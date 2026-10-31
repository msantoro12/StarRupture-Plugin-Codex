#pragma once

// Where pinned recipe windows go on screen. A pure function of the screen
// size, the windows' sizes, the lead pin's position, the arrangement and the
// gap: no ImGui, no game, so it can be tested on its own.
//
// Grouped (the default): the first pin is the lead and the only one the
// player moves; the rest tile or cascade under it. Ungrouped: every pin has
// its own spot, and a new pin is placed by PlaceNew. Every window stays fully
// on screen.

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
		// Rounded: the window is placed on whole pixels, so a spot saved from it
		// comes back to the same pixel.
		return ClampToScreen(p, size, { std::round(fx * p.screenW), std::round(fy * p.screenH) });
	}

	// A window's top-left corner as fractions of the screen, kept inside [0, 1],
	// for saving; LeadFromFraction turns it back into a spot at any resolution.
	inline Point ToFraction(const Params& p, Point at)
	{
		return { std::clamp(at.x / p.screenW, 0.0f, 1.0f), std::clamp(at.y / p.screenH, 0.0f, 1.0f) };
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
				Point at = ClampToScreen(p, sizes[k], columns ? Point{ b, cur } : Point{ cur, b });
				// The lead's title bar is its handle: a window that would cover it
				// starts past the lead along its line instead.
				if (k > 0 && at.x < lead.x + sizes[0].w && at.x + sizes[k].w > lead.x &&
					at.y < lead.y + p.frame && at.y + sizes[k].h > lead.y)
				{
					cur = columns ? lead.y + p.frame + gap : lead.x + sizes[0].w + gap;
					at = ClampToScreen(p, sizes[k], columns ? Point{ b, cur } : Point{ cur, b });
				}
				out[k] = at;
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

	namespace detail
	{
		// Where the arrangement would put a window of `size` next to the pin
		// at `ref`: below it in a column (Right lines up on its right edge,
		// Left on its left), after it in a row for Top, one step down and in
		// for a cascade.
		inline Point Beside(const Params& p, Point ref, Size refSize, Size size)
		{
			if (p.cascade)
				return { ref.x + (p.edge == Edge::Right ? -p.step : p.step), ref.y + p.step };
			const float gap = p.gap ? p.step - p.frame : 0.0f;
			if (p.edge == Edge::Top)
				return { ref.x + refSize.w + gap, ref.y };
			return { p.edge == Edge::Right ? ref.x + refSize.w - size.w : ref.x, ref.y + refSize.h + gap };
		}

		// Whether a window of `size` at `cand` is fully on screen and covers no
		// pin. A cascade is meant to overlap, so there only the pins' title
		// bars count.
		inline bool Free(const Params& p, const Size* sizes, const Point* at, int count, Point cand, Size size)
		{
			constexpr float eps = 0.01f;
			if (cand.x < -eps || cand.y < -eps || cand.x + size.w > p.screenW + eps || cand.y + size.h > p.screenH + eps)
				return false;
			for (int k = 0; k < count; ++k)
			{
				const float otherH = p.cascade ? (std::min)(sizes[k].h, p.frame) : sizes[k].h;
				if (cand.x < at[k].x + sizes[k].w - eps && cand.x + size.w > at[k].x + eps &&
					cand.y < at[k].y + otherH - eps && cand.y + size.h > at[k].y + eps)
					return false;
			}
			return true;
		}
	}

	// Where a newly pinned window of `size` goes when the pins are ungrouped.
	// `sizes` and `at` are the `count` pins already up, in pin order. It goes
	// where the arrangement would put it next to the most recently placed pin
	// (the last), if that is fully on screen and covers no pin; otherwise at
	// the next free arrangement slot (next to an earlier pin, a new line at
	// the margin on whichever side has room, the cascade's other direction),
	// and last of all at the default spot.
	inline Point PlaceNew(const Params& p, const Size* sizes, const Point* at, int count, Size size)
	{
		if (count <= 0)
			return DefaultLead(p, size);

		// Each candidate is kept on screen first, as the grouped layout does, so a
		// window wider than the room beside a pin still finds its spot.
		for (int k = count - 1; k >= 0; --k)
		{
			const Point cand = ClampToScreen(p, size, detail::Beside(p, at[k], sizes[k], size));
			if (detail::Free(p, sizes, at, count, cand, size))
				return cand;
		}

		const float gap = p.gap ? p.step - p.frame : 0.0f;
		float minX = 1.0e30f, minY = 1.0e30f, maxX = -1.0e30f, maxY = -1.0e30f;
		for (int k = 0; k < count; ++k)
		{
			minX = (std::min)(minX, at[k].x);
			minY = (std::min)(minY, at[k].y);
			maxX = (std::max)(maxX, at[k].x + sizes[k].w);
			maxY = (std::max)(maxY, at[k].y + sizes[k].h);
		}

		Point more[2];
		int moreCount = 0;
		if (p.cascade)
		{
			const Point last = at[count - 1];
			more[moreCount++] = { last.x + (p.edge == Edge::Right ? p.step : -p.step), last.y + p.step };
		}
		else if (p.edge == Edge::Top)
		{
			more[moreCount++] = { p.step, minY - gap - size.h };
			more[moreCount++] = { p.step, maxY + gap };
		}
		else
		{
			const Point left{ minX - gap - size.w, p.step };
			const Point right{ maxX + gap, p.step };
			more[moreCount++] = p.edge == Edge::Right ? left : right;
			more[moreCount++] = p.edge == Edge::Right ? right : left;
		}
		for (int i = 0; i < moreCount; ++i)
		{
			more[i] = ClampToScreen(p, size, more[i]);
			if (detail::Free(p, sizes, at, count, more[i], size))
				return more[i];
		}

		return DefaultLead(p, size);
	}

	// Every pin placed one after the other as if just pinned: the first at the
	// default spot, the rest by PlaceNew.
	inline void SpawnAll(const Params& p, const Size* sizes, int count, Point* out)
	{
		if (count > kMaxPins)
			count = kMaxPins;
		for (int k = 0; k < count; ++k)
			out[k] = PlaceNew(p, sizes, out, k, sizes[k]);
	}
}
