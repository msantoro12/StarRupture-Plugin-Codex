#pragma once

// Where pinned recipe windows go on screen. A pure function of the screen
// size, the windows' sizes, the lead pin's position, the arrangement and the
// gap: no ImGui, no game, so it can be tested on its own.
//
// The first pin is the lead and the only one the player moves. The rest tile
// or cascade under it. Every window stays fully on screen.

#include <algorithm>
#include <cmath>

namespace CodexPinLayout
{
	constexpr int kMaxPins = 8;

	struct Point
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct Size
	{
		float w = 0.0f;
		float h = 0.0f;
	};

	enum class Edge { Right, Left, Top };

	struct Params
	{
		float screenW = 0.0f;
		float screenH = 0.0f;
		float step = 0.0f;  // the theme's frame height with spacing: the margin, the cascade step
		float frame = 0.0f; // the theme's frame height; step less frame is the item spacing
		Edge  edge = Edge::Right;
		bool  cascade = false;
		bool  gap = true;   // tiles: leave the item spacing between windows; off packs them edge to edge
	};

	// A window's top-left corner moved the least that puts all of it on
	// screen (a window larger than the screen sits at 0).
	inline Point ClampToScreen(const Params& p, Size size, Point at)
	{
		return { std::clamp(at.x, 0.0f, (std::max)(0.0f, p.screenW - size.w)),
			std::clamp(at.y, 0.0f, (std::max)(0.0f, p.screenH - size.h)) };
	}

	// Where the lead starts when the player has not moved it: one step in
	// from the configured edge, one step down.
	inline Point DefaultLead(const Params& p, Size size)
	{
		const float x = p.edge == Edge::Right ? p.screenW - p.step - size.w : p.step;
		return ClampToScreen(p, size, { x, p.step });
	}

	// The lead's saved spot: its top-left corner as fractions of the screen,
	// so it survives a resolution change, kept on screen at the new size.
	inline Point LeadFromFraction(const Params& p, Size size, float fx, float fy)
	{
		return ClampToScreen(p, size, { fx * p.screenW, fy * p.screenH });
	}

	namespace detail
	{
		// How far a cascade can step in one direction before some window
		// leaves [0, limit]; `at` is the lead's corner, `size(k)` the extent
		// of window k along the axis.
		inline float CascadeReach(const float* extent, int count, float at, float limit, float sign)
		{
			float reach = 1.0e30f;
			for (int k = 1; k < count; ++k)
			{
				const float room = sign > 0.0f ? limit - extent[k] - at : at;
				reach = (std::min)(reach, room / static_cast<float>(k));
			}
			return reach;
		}

		inline void Cascade(const Params& p, const Size* sizes, int count, Point& lead, Point* out)
		{
			float w[kMaxPins], h[kMaxPins];
			for (int k = 0; k < count; ++k)
			{
				w[k] = sizes[k].w;
				h[k] = sizes[k].h;
			}

			// Towards the middle of the screen for the Right edge, outwards from
			// it otherwise; the other way when that runs off the screen.
			const float prefer = p.edge == Edge::Right ? -1.0f : 1.0f;
			const float reachPreferred = CascadeReach(w, count, lead.x, p.screenW, prefer);
			const float reachOther = CascadeReach(w, count, lead.x, p.screenW, -prefer);
			const float sx = reachPreferred >= p.step || reachPreferred >= reachOther ? prefer : -prefer;
			const float stepX = std::clamp(sx == prefer ? reachPreferred : reachOther, 0.0f, p.step);

			// When the stack does not fit below the lead, the whole cascade, lead
			// included, moves up by the shortfall so every title bar stays showing.
			float fitH = 0.0f;
			for (int k = 0; k < count; ++k)
				fitH = (std::max)(fitH, h[k] + p.step * static_cast<float>(k));
			lead.y = (std::max)(0.0f, (std::min)(lead.y, p.screenH - fitH));

			// Downwards only: a later pin sits in front of the earlier ones, so
			// stepping up would hide their title bars. Closer steps when the
			// screen is short.
			const float stepY = std::clamp(CascadeReach(h, count, lead.y, p.screenH, 1.0f), 0.0f, p.step);

			for (int k = 0; k < count; ++k)
			{
				const float fk = static_cast<float>(k);
				out[k] = ClampToScreen(p, sizes[k], { lead.x + sx * stepX * fk, lead.y + stepY * fk });
			}
		}

		// Tiles run in lines. Right and Left lines are columns going down;
		// Top lines are rows going across. A window that would cross the
		// margin starts a new line at the margin, on whichever side of the
		// lines so far has room. `a` runs along a line, `b` across lines.
		inline void Tile(const Params& p, const Size* sizes, int count, Point lead, Point* out)
		{
			const bool columns = p.edge != Edge::Top;
			const float limitA = columns ? p.screenH : p.screenW;
			const float limitB = columns ? p.screenW : p.screenH;
			const float margin = p.step;
			const float gap = p.gap ? p.step - p.frame : 0.0f;

			auto extentA = [&](int k) { return columns ? sizes[k].h : sizes[k].w; };
			auto extentB = [&](int k) { return columns ? sizes[k].w : sizes[k].h; };

			// The lead column on the Right edge lines up on the lead's right
			// edge; every other first line lines up on the lead's start.
			bool alignEnd = p.edge == Edge::Right;
			float lineB = columns ? (alignEnd ? lead.x + sizes[0].w : lead.x) : lead.y;
			float cur = columns ? lead.y : lead.x;
			int inLine = 0;
			float lo = 1.0e30f, hi = -1.0e30f; // the span across all lines placed so far
			float lineLo = 1.0e30f, lineHi = -1.0e30f; // the span of the current line
			float dir = 0.0f;                  // which way new lines go: -1 before, +1 after

			for (int k = 0; k < count; ++k)
			{
				if (inLine > 0 && cur + extentA(k) > limitA - margin)
				{
					lo = (std::min)(lo, lineLo);
					hi = (std::max)(hi, lineHi);

					const float prefer = dir != 0.0f ? dir : (p.edge == Edge::Right ? -1.0f : 1.0f);
					// The whole line's width decides which side has room for it.
					float lineExtent = 0.0f, along = margin;
					for (int j = k; j < count && (j == k || along + extentA(j) <= limitA - margin); ++j)
					{
						lineExtent = (std::max)(lineExtent, extentB(j));
						along += extentA(j) + gap;
					}
					const float need = lineExtent + gap;
					const float roomPrefer = prefer < 0.0f ? lo : limitB - hi;
					const float roomOther = prefer < 0.0f ? limitB - hi : lo;
					dir = roomPrefer >= need || roomPrefer >= roomOther ? prefer : -prefer;

					alignEnd = dir < 0.0f;
					lineB = dir < 0.0f ? lo - gap : hi + gap;
					cur = margin;
					lineLo = 1.0e30f;
					lineHi = -1.0e30f;
					inLine = 0;
				}

				const float b = alignEnd ? lineB - extentB(k) : lineB;
				// Kept on screen as it is placed, so the line's span is the real one.
				out[k] = ClampToScreen(p, sizes[k], columns ? Point{ b, cur } : Point{ cur, b });
				cur += extentA(k) + gap;
				const float placedB = columns ? out[k].x : out[k].y;
				lineLo = (std::min)(lineLo, placedB);
				lineHi = (std::max)(lineHi, placedB + extentB(k));
				++inLine;
			}
		}
	}

	// Whether the lead's placement has to be requested this frame. A request
	// still pending (a drag or an anchor) is always answered with the laid-out
	// spot: Arrange can move the lead off the requested one (a cascade lifts
	// it), and comparing with the window's measured spot instead would send
	// the lifted spot only every other frame, so the lead would flicker. With
	// nothing pending, only a window that has drifted from its spot is moved.
	inline bool LeadNeedsRequest(bool pending, Point placed, Point measured, float threshold)
	{
		return pending || std::fabs(placed.x - measured.x) > threshold || std::fabs(placed.y - measured.y) > threshold;
	}

	// Positions for `count` windows (at most kMaxPins) in pin order; sizes[0]
	// and lead are the lead's. The lead is kept on screen; a cascade that would
	// not fit below it lifts the lead too.
	inline void Arrange(const Params& p, const Size* sizes, int count, Point lead, Point* out)
	{
		if (count <= 0)
			return;

		lead = ClampToScreen(p, sizes[0], lead);
		if (count > kMaxPins)
			count = kMaxPins;
		out[0] = lead;
		if (p.cascade)
			detail::Cascade(p, sizes, count, lead, out);
		else
			detail::Tile(p, sizes, count, lead, out);
		out[0] = lead;
	}
}
