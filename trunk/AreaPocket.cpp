// AreaPocket.cpp
// Copyright 2011, Dan Heeks
// This program is released under the BSD license. See the file COPYING for details.

// implements CArea::MakeOnePocketCurve

#include "Area.h"

#include <map>

static const CAreaPocketParams* pocket_params = NULL;

class IslandAndOffset
{
public:
	const CCurve* island;
	CArea offset;
	std::list<CCurve> island_inners;

	IslandAndOffset(const CCurve* Island)
	{
		island = Island;

		offset.m_curves.push_back(*island);
		offset.m_curves.back().Reverse();
		offset.Offset(-pocket_params->stepover);

		if(offset.m_curves.size() > 1)
		{
			for(std::list<CCurve>::iterator It = offset.m_curves.begin(); It != offset.m_curves.end(); It++)
			{
				if(It == offset.m_curves.begin())continue;
				island_inners.push_back(*It);
			}
			offset.m_curves.resize(1);
		}
	}
};

class CurveTree
{
	static std::list<CurveTree*> to_do_list_for_MakeOffsets;
	void MakeOffsets2();

public:
	Point point_on_parent;
	CCurve curve;
	std::list<CurveTree*> inners;
	std::list<const IslandAndOffset*> offset_islands;
	CurveTree(const CCurve &c)
	{
		curve = c;
	}
	~CurveTree(){}

	void MakeOffsets();
};

class GetCurveItem
{
public:
	CurveTree* curve_tree;
	std::list<CVertex>::iterator EndIt;
	static std::list<GetCurveItem> to_do_list;

	GetCurveItem(CurveTree* ct, std::list<CVertex>::iterator EIt):curve_tree(ct), EndIt(EIt){}

	void GetCurve(CCurve& output);
	CVertex& back(){std::list<CVertex>::iterator It = EndIt; It--; return *It;}
};

std::list<GetCurveItem> GetCurveItem::to_do_list;
std::list<CurveTree*> CurveTree::to_do_list_for_MakeOffsets;

void GetCurveItem::GetCurve(CCurve& output)
{
	// walk around the curve adding spans to output until we get to an inner's point_on_parent
	// then add a line from the inner's point_on_parent to inner's start point, then GetCurve from inner

	// add start point
	if(CArea::m_please_abort)return;
	output.m_vertices.insert(this->EndIt, CVertex(curve_tree->curve.m_vertices.front()));

	std::list<CurveTree*> inners_to_visit;
	for(std::list<CurveTree*>::iterator It2 = curve_tree->inners.begin(); It2 != curve_tree->inners.end(); It2++)
	{
		inners_to_visit.push_back(*It2);
	}

	const Point* prev_p = NULL;
	for(std::list<CVertex>::iterator It = curve_tree->curve.m_vertices.begin(); It != curve_tree->curve.m_vertices.end(); It++)
	{
		const CVertex& vertex = *It;
		if(prev_p)
		{
			Span span(*prev_p, vertex);

			// order inners on this span
			std::multimap<double, CurveTree*> ordered_inners;
			for(std::list<CurveTree*>::iterator It2 = inners_to_visit.begin(); It2 != inners_to_visit.end();)
			{
				CurveTree *inner = *It2;
				double t;
				if(span.On(inner->point_on_parent, &t))
				{
					ordered_inners.insert(std::make_pair(t, inner));
					It2 = inners_to_visit.erase(It2);
				}
				else
				{
					It2++;
				}
				if(CArea::m_please_abort)return;
			}

			if(CArea::m_please_abort)return;
			for(std::multimap<double, CurveTree*>::iterator It2 = ordered_inners.begin(); It2 != ordered_inners.end(); It2++)
			{
				CurveTree& inner = *(It2->second);
				if(inner.point_on_parent != back().m_p)
				{
					output.m_vertices.insert(this->EndIt, CVertex(vertex.m_type, inner.point_on_parent, vertex.m_c));
				}
				if(CArea::m_please_abort)return;

				// vertex add after GetCurve
				std::list<CVertex>::iterator VIt = output.m_vertices.insert(this->EndIt, CVertex(inner.point_on_parent));

				//inner.GetCurve(output);
				GetCurveItem::to_do_list.push_back(GetCurveItem(&inner, VIt));
			}

			if(back().m_p != vertex.m_p)output.m_vertices.insert(this->EndIt, vertex);
		}
		prev_p = &vertex.m_p;
	}

	if(CArea::m_please_abort)return;
	for(std::list<CurveTree*>::iterator It2 = inners_to_visit.begin(); It2 != inners_to_visit.end(); It2++)
	{
		CurveTree &inner = *(*It2);
		if(inner.point_on_parent != back().m_p)
		{
			output.m_vertices.insert(this->EndIt, CVertex(inner.point_on_parent));
		}
		if(CArea::m_please_abort)return;

		// vertex add after GetCurve
		std::list<CVertex>::iterator VIt = output.m_vertices.insert(this->EndIt, CVertex(inner.point_on_parent));

		//inner.GetCurve(output);
		GetCurveItem::to_do_list.push_back(GetCurveItem(&inner, VIt));

	}
}

void CurveTree::MakeOffsets2()
{
	// make offsets

	if(CArea::m_please_abort)return;
	CArea smaller;
	smaller.m_curves.push_back(curve);
	smaller.Offset(pocket_params->stepover);

	if(CArea::m_please_abort)return;

	// test islands
	for(std::list<const IslandAndOffset*>::iterator It = offset_islands.begin(); It != offset_islands.end();)
	{
		const IslandAndOffset* island_and_offset = *It;

		if(GetOverlapType(island_and_offset->offset, smaller) == eInside)
			It++; // island is still inside
		else
		{
			inners.push_back(new CurveTree(*island_and_offset->island));
			inners.back()->point_on_parent = curve.NearestPoint(*island_and_offset->island);
			if(CArea::m_please_abort)return;
			Point island_point = island_and_offset->island->NearestPoint(inners.back()->point_on_parent);
			if(CArea::m_please_abort)return;
			inners.back()->curve.ChangeStart(island_point);
			if(CArea::m_please_abort)return;

			// add the island offset's inner curves
			for(std::list<CCurve>::const_iterator It2 = island_and_offset->island_inners.begin(); It2 != island_and_offset->island_inners.end(); It2++)
			{
				const CCurve& island_inner = *It2;
				inners.back()->inners.push_back(new CurveTree(island_inner));
				inners.back()->inners.back()->point_on_parent = inners.back()->curve.NearestPoint(island_inner);
				if(CArea::m_please_abort)return;
				Point island_point = island_inner.NearestPoint(inners.back()->inners.back()->point_on_parent);
				if(CArea::m_please_abort)return;
				inners.back()->inners.back()->curve.ChangeStart(island_point);
				if(CArea::m_please_abort)return;
			}

			smaller.Subtract(island_and_offset->offset);
			if(CArea::m_please_abort)return;
			It = offset_islands.erase(It);
			if(offset_islands.size() == 0)break;
			It = offset_islands.begin();
		}
	}

	CArea::m_processing_done += CArea::m_MakeOffsets_increment;
	if(CArea::m_processing_done > CArea::m_after_MakeOffsets_length)CArea::m_processing_done = CArea::m_after_MakeOffsets_length;

	std::list<CArea> separate_areas;
	smaller.Split(separate_areas);
	if(CArea::m_please_abort)return;
	for(std::list<CArea>::iterator It = separate_areas.begin(); It != separate_areas.end(); It++)
	{
		CArea& separate_area = *It;
		CCurve& first_curve = separate_area.m_curves.front();
		inners.push_back(new CurveTree(first_curve));

		for(std::list<const IslandAndOffset*>::iterator It = offset_islands.begin(); It != offset_islands.end();It++)
		{
			const IslandAndOffset* island_and_offset = *It;
			if(GetOverlapType(island_and_offset->offset, separate_area) == eInside)
				inners.back()->offset_islands.push_back(island_and_offset);
			if(CArea::m_please_abort)return;
		}

		inners.back()->point_on_parent = curve.NearestPoint(first_curve);
		if(CArea::m_please_abort)return;
		Point first_curve_point = first_curve.NearestPoint(inners.back()->point_on_parent);
		if(CArea::m_please_abort)return;
		inners.back()->curve.ChangeStart(first_curve_point);
		if(CArea::m_please_abort)return;
		//inners.back().MakeOffsets2(); // recursive
		to_do_list_for_MakeOffsets.push_back(inners.back()); // do it later, in a while loop
		if(CArea::m_please_abort)return;
	}
}

void CurveTree::MakeOffsets()
{
	to_do_list_for_MakeOffsets.push_back(this);

	while(to_do_list_for_MakeOffsets.size() > 0)
	{
		CurveTree* curve_tree = to_do_list_for_MakeOffsets.front();
		to_do_list_for_MakeOffsets.pop_front();
		curve_tree->MakeOffsets2();
	}
}

void recur(std::list<CArea> &arealist, const CArea& a1, const CAreaPocketParams &params, int level)
{
	//if(level > 3)return;

    // this makes arealist by recursively offsetting a1 inwards
    
	if(a1.m_curves.size() == 0)
		return;
    
	if(params.from_center)
		arealist.push_front(a1);
	else
		arealist.push_back(a1);

    CArea a_offset = a1;
    a_offset.Offset(params.stepover);
    
    // split curves into new areas
	if(CArea::HolesLinked())
	{
		for(std::list<CCurve>::iterator It = a_offset.m_curves.begin(); It != a_offset.m_curves.end(); It++)
		{
            CArea a2;
			a2.m_curves.push_back(*It);
            recur(arealist, a2, params, level + 1);
		}
	}
    else
	{
        // split curves into new areas
		a_offset.Reorder();
        CArea* a2 = NULL;
       
		for(std::list<CCurve>::iterator It = a_offset.m_curves.begin(); It != a_offset.m_curves.end(); It++)
		{
			CCurve& curve = *It;
			if(curve.IsClockwise())
			{
				if(a2 != NULL)
					a2->m_curves.push_back(curve);
			}
			else
			{
				if(a2 != NULL)
					recur(arealist, *a2, params, level + 1);
				else
					a2 = new CArea();
                a2->m_curves.push_back(curve);
			}
		}

		if(a2 != NULL)
			recur(arealist, *a2, params, level + 1);
	}
}

static CArea make_obround(const Point& p0, const Point& p1, double radius)
{
    Point dir = p1 - p0;
    double d = dir.length();
    dir.normalize();
    Point right(dir.y, -dir.x);
    CArea obround;
    CCurve c;
	if(fabs(radius) < 0.0000001)radius = (radius > 0.0) ? 0.002 : (-0.002);
    Point vt0 = p0 + right * radius;
    Point vt1 = p1 + right * radius;
    Point vt2 = p1 - right * radius;
    Point vt3 = p0 - right * radius;
	c.append(vt0);
    c.append(vt1);
    c.append(CVertex(1, vt2, p1));
    c.append(vt3);
    c.append(CVertex(1, vt0, p0));
    obround.append(c);
    return obround;
}
    
static bool feed_possible(const CArea &area_for_feed_possible, const Point& p0, const Point& p1, double tool_radius)
{
    CArea obround = make_obround(p0, p1, tool_radius);
    obround.Subtract(area_for_feed_possible);
	return obround.m_curves.size() == 0;
}

void CArea::MakeOnePocketCurve(std::list<CCurve> &curve_list, const CAreaPocketParams &params)const
{
	if(CArea::m_please_abort)return;
#if 0  // simple offsets with feed or rapid joins
	CArea area_for_feed_possible = *this;

	area_for_feed_possible.Offset(-params.tool_radius - 0.01);
	CArea a_offset = *this;

	std::list<CArea> arealist;
	recur(arealist, a_offset, params, 0);

	bool first = true;

	for(std::list<CArea>::iterator It = arealist.begin(); It != arealist.end(); It++)
	{
		CArea& area = *It;
		for(std::list<CCurve>::iterator It = area.m_curves.begin(); It != area.m_curves.end(); It++)
		{
			CCurve& curve = *It;
			if(!first)
			{
				// try to join these curves with a feed move, if possible and not too long
				CCurve &prev_curve = curve_list.back();
				const Point &prev_p = prev_curve.m_vertices.back().m_p;
				const Point &next_p = curve.m_vertices.front().m_p;

				if(feed_possible(area_for_feed_possible, prev_p, next_p, params.tool_radius))
				{
					// join curves
					prev_curve += curve;
				}
				else
				{
					curve_list.push_back(curve);
				}
			}
			else
			{
				curve_list.push_back(curve);
			}
			first = false;
		}
	}
#else
	pocket_params = &params;
	if(m_curves.size() == 0)
	{
		CArea::m_processing_done += CArea::m_single_area_processing_length;
		return;
	}
	CurveTree top_level(m_curves.front());

	std::list<IslandAndOffset> offset_islands;

	for(std::list<CCurve>::const_iterator It = m_curves.begin(); It != m_curves.end(); It++)
	{
		const CCurve& c = *It;
		if(It != m_curves.begin())
		{
			IslandAndOffset island_and_offset(&c);
			offset_islands.push_back(island_and_offset);
			top_level.offset_islands.push_back(&(offset_islands.back()));
			if(m_please_abort)return;
		}
	}

	CArea::m_processing_done += CArea::m_single_area_processing_length * 0.1;

	double MakeOffsets_processing_length = CArea::m_single_area_processing_length * 0.8;
	CArea::m_after_MakeOffsets_length = CArea::m_processing_done + MakeOffsets_processing_length;
	double guess_num_offsets = sqrt(GetArea(true)) * 0.5 / params.stepover;
	CArea::m_MakeOffsets_increment = MakeOffsets_processing_length / guess_num_offsets;

	top_level.MakeOffsets();
	if(CArea::m_please_abort)return;
	CArea::m_processing_done = CArea::m_after_MakeOffsets_length;

	curve_list.push_back(CCurve());
	CCurve& output = curve_list.back();

	GetCurveItem::to_do_list.push_back(GetCurveItem(&top_level, output.m_vertices.end()));

	while(GetCurveItem::to_do_list.size() > 0)
	{
		GetCurveItem item = GetCurveItem::to_do_list.front();
		item.GetCurve(output);
		GetCurveItem::to_do_list.pop_front();
	}

	// delete curve_trees non-recursively
	std::list<CurveTree*> CurveTreeDestructList;
	for(std::list<CurveTree*>::iterator It = top_level.inners.begin(); It != top_level.inners.end(); It++)
	{
		CurveTreeDestructList.push_back(*It);
	}
	while(CurveTreeDestructList.size() > 0)
	{
		CurveTree* curve_tree = CurveTreeDestructList.front();
		CurveTreeDestructList.pop_front();
		for(std::list<CurveTree*>::iterator It = curve_tree->inners.begin(); It != curve_tree->inners.end(); It++)
		{
			CurveTreeDestructList.push_back(*It);
		}
		delete curve_tree;
	}

	CArea::m_processing_done += CArea::m_single_area_processing_length * 0.1;
#endif
}

