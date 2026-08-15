// ImGui Bezier widget. @r-lyeh, public domain
// v1.03: improve grabbing, confine grabbers to area option, adaptive size, presets, preview.
// v1.02: add BezierValue(); comments; usage
// v1.01: out-of-bounds coord snapping; custom border width; spacing; cosmetics
// v1.00: initial version
//
// [ref] http://robnapier.net/faster-bezier
// [ref] http://easings.net/es#easeInSine
//
// Usage:
// {  static float v[5] = { 0.390f, 0.575f, 0.565f, 1.000f }; 
//    ImGui::Bezier( "easeOutSine", v );       // draw
//    float y = ImGui::BezierValue( 0.5f, v ); // x delta in [0..1] range
// }

#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

namespace ImGui
{

    ImVec2 scaleUnitToEditor(float x, float y, ImRect const& bb)
    {
        auto pos = ImVec2(x, y);
        pos.y = 1 - pos.y;

        pos *= (bb.Max - bb.Min);
        pos += bb.Min;

        return pos;
    }

    ImVec2 scaleToEditor(float x, float y, float scaleMinX, float scaleMinY, float scaleMaxX, float scaleMaxY, ImRect const& bb)
    {
        auto pos = ImVec2(x, y);

        pos.x -= scaleMinX;
        pos.y -= scaleMinY;
        
        pos.x /= (scaleMaxX - scaleMinX);
        pos.y /= (scaleMaxY - scaleMinY);

        pos.y = 1 - pos.y;

        pos *= (bb.Max - bb.Min);
        pos += bb.Min;

        return pos;
    }

    ImVec2 scaleFromEditor(ImVec2 const& p, float scaleMinX, float scaleMinY, float scaleMaxX, float scaleMaxY, ImRect const& bb)
    {
        auto pos = p;
        
        pos.x *= (scaleMaxX - scaleMinX);
        pos.x += scaleMinX;

        pos.y *= (scaleMaxY - scaleMinY);
        pos.y += scaleMinY;

        return pos;
    }

    static bool gMultiCurveDraggingPoint{ false };

    bool MultiCurve(
        char const* label, 
        ImVec2* points, 
        int* numPoints, 
        int maxPoints, 
        int* editPoint, 
        bool snap, 
        float scaleMinX,
        float scaleMinY,
        float scaleMaxX,
        float scaleMaxY,
        std::vector<std::vector<std::pair<float, float>>> const& renderValues, 
        float proxyValue,
        float curValue,
        int widgetHeight, 
        int* widgetWidth, 
        bool* clicked,
        bool* released)
    {
        const float CURVE_WIDTH{ 3.0f };
        const float GRAB_RADIUS{ 4.0f };
        const float GRAB_BORDER{ 2.0f };

        const ImGuiStyle& Style = GetStyle();
        const ImGuiIO& IO = GetIO();
        ImDrawList* drawList = GetWindowDrawList();
        ImGuiWindow* window = GetCurrentWindow();
        
        *editPoint = -1;

        *clicked = false;
        *released = false;

        if (window->SkipItems)
        {
            return false;
        }

        if (*numPoints < 2)
        {
            return false;
        }

        // Header and spacing
        auto hovered = IsItemActive() || IsItemHovered();
        bool changed{ false };
        
        Dummy(ImVec2(0, 3));

        // Prepare canvas
        const float availableWidth = GetContentRegionAvail().x;
        const float dim = (*widgetWidth != 0) ? *widgetWidth : availableWidth;
        ImVec2 canvas(dim, (float)widgetHeight);

        if (widgetWidth)
        {
            *widgetWidth = (int)dim;
        }

        ImRect bb(window->DC.CursorPos, window->DC.CursorPos + canvas);

        ItemSize(bb);
        if (!ItemAdd(bb, NULL))
        {
            return changed;
        }

        float gridX = 10;
        float gridY = 10;

        const ImGuiID id = window->GetID(label);
        hovered |= ItemHoverable(ImRect(bb.Min, bb.Max), id);

        RenderFrame(bb.Min, bb.Max, GetColorU32(ImGuiCol_FrameBg, 1), true, Style.FrameRounding);

        // Background grid
        for (float i = 0; i <= canvas.x; i += (canvas.x / gridX)) 
        {
            drawList->AddLine(
                ImVec2(bb.Min.x + i, bb.Min.y),
                ImVec2(bb.Min.x + i, bb.Max.y),
                GetColorU32(ImGuiCol_TextDisabled));
        }

        for (float i = 0; i <= canvas.y; i += (canvas.y / gridY))
        {
            drawList->AddLine(
                ImVec2(bb.Min.x, bb.Min.y + i),
                ImVec2(bb.Max.x, bb.Min.y + i),
                GetColorU32(ImGuiCol_TextDisabled));
        }

        // Get closest point
        ImVec2 mouse = GetIO().MousePos;

        int selected{ -1 }, closest{ -1 };
        float closestDist{ 1e10f };
        for (int i = 0; i < *numPoints; ++i) 
        {
            // Scale y down to unit, flip, then back up to window
            auto pos = scaleToEditor(points[i].x, points[i].y, scaleMinX, scaleMinY, scaleMaxX, scaleMaxY, bb);
            float distance = (pos.x - mouse.x) * (pos.x - mouse.x) + (pos.y - mouse.y) * (pos.y - mouse.y);

            if (distance < closestDist)
            {
                closest = i;
                closestDist = distance;
            }
        }

        if (hovered)
        {
            if (closestDist < (8 * GRAB_RADIUS * GRAB_RADIUS))
            {
                selected = closest;
         
                if (IsMouseClicked(0))
                {
                    *clicked = true;
                    gMultiCurveDraggingPoint = true;
                }
                else if (IsMouseReleased(0))
                {
                    if (gMultiCurveDraggingPoint)
                    {
                        *released = true;
                        gMultiCurveDraggingPoint = false;
                        *editPoint = selected;
                    }
                }

                SetTooltip("(%3.2f, %2.1f)", points[selected].x, points[selected].y);

                auto lockX = IO.KeyShift || selected == 0 || selected == (*numPoints - 1);

                if (IsMouseClicked(0) || IsMouseDragging(0))
                {
                    if (!lockX)
                    {
                        // Don't move endpoints horizontally, and clamp the others
                        auto dx = (GetIO().MouseDelta.x / canvas.x) * (scaleMaxX - scaleMinX);
                        points[selected].x += dx;

                        if (points[selected].x < points[selected - 1].x)
                        {
                            points[selected].x = points[selected - 1].x;
                        }
                        if (points[selected].x > points[selected + 1].x)
                        {
                            points[selected].x = points[selected + 1].x;
                        }
                    }

                    points[selected].y -= (GetIO().MouseDelta.y / canvas.y) * (scaleMaxY - scaleMinY);

                    if (snap)
                    {
                        throw std::exception("Snap-to-grid not yet implemented");
                    }

                    auto& py = points[selected].y;
                    py = (py < scaleMinY ? scaleMinY : (py > scaleMaxY ? scaleMaxY : py));

                    *editPoint = selected;
                    changed = true;
                }
                else if (IsMouseClicked(1))
                {
                    // Delete point
                    int np = *numPoints;
                    if (selected > 0 && selected < (np - 1))
                    {
                        for (int i = selected; i < np - 1; ++i)
                        {
                            points[i] = points[i + 1];
                        }

                        *numPoints = np - 1;
                        *editPoint = selected;
                        changed = true;
                    }
                }
            }
            else
            {
                // Add a point
                if (IsMouseClicked(0))
                {
                    int np = *numPoints;
                    if (np < maxPoints)
                    {
                        auto p = mouse - bb.Min;

                        p.x /= canvas.x;
                        p.y = 1.0f - p.y / canvas.y;

                        for (int i = 0; i < np; ++i)
                        {
                            if (points[i].x <= p.x && p.x < points[i + 1].x)
                            {
                                for (int j = np; j > i + 1; --j)
                                {
                                    points[j] = points[j - 1];
                                }

                                auto newp = scaleFromEditor(p, scaleMinX, scaleMinY, scaleMaxX, scaleMaxY, bb);
                                points[i + 1] = newp;
                                *editPoint = i + 1;
                                *numPoints = np + 1;
                                changed = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
        else
        {
            if (gMultiCurveDraggingPoint)
            {
                *released = true;
                gMultiCurveDraggingPoint = false;
            }
        }

        // Draw curve
        ImColor curveColour(GetStyle().Colors[ImGuiCol_PlotLines]);
        
        for (int i = 0; i < (int)renderValues.size(); ++i)
        {
            auto const& rv = renderValues[i];
            
            for (uint32_t j = 0; j < (uint32_t)rv.size() - 1; ++j)
            {
                auto const& rv0 = rv[j + 0];
                auto const& rv1 = rv[j + 1];

                auto v0 = scaleUnitToEditor(rv0.first, rv0.second, bb);
                auto v1 = scaleUnitToEditor(rv1.first, rv1.second, bb);

                ImVec4 cc = curveColour;
                if (rv0.first == rv1.first)
                {
                    cc.x = 0.9f;
                }
                else
                {
                    cc.y = 0.9f;
                }

                drawList->AddLine(v0, v1, ImColor(cc), CURVE_WIDTH);
            }
        }
        
        // Draw timelines
        if (proxyValue >= 0.0f && proxyValue <= 1.0f)
        {
            float tlp0 = (bb.Max.x - bb.Min.x) * proxyValue;
            drawList->AddLine(bb.Min + ImVec2(tlp0, 0), bb.Min + ImVec2(tlp0, (bb.Max.y - bb.Min.y)), ImColor(0.9f, 0.2f, 0.5f, 1.0f), 1.0f);
        }

        if (curValue >= 0.0f && curValue <= 1.0f)
        {
            float tlp1 = (bb.Max.x - bb.Min.x) * curValue;
            drawList->AddLine(bb.Min + ImVec2(tlp1, 0), bb.Min + ImVec2(tlp1, (bb.Max.y - bb.Min.y)), ImColor(0.2f, 0.9f, 0.5f, 1.0f), 1.0f);
        }

        // Draw points
        float luma = IsItemActive() || IsItemHovered() ? 0.5f : 1.0f;
        ImVec4 white(GetStyle().Colors[ImGuiCol_Text]);
        ImVec4 pink(1.0f, 0.0f, 0.75f, luma), red(1.0f, 0.0f, 1.0f, luma);

        int np = *numPoints;

        for (int i = 0; i < np; ++i)
        {
            ImVec2 p = scaleToEditor(points[i].x, points[i].y, scaleMinX, scaleMinY, scaleMaxX, scaleMaxY, bb);

            if (i == selected)
            {
                drawList->AddCircleFilled(p, GRAB_RADIUS, ImColor(red));
            }
            else
            {
                drawList->AddCircleFilled(p, GRAB_RADIUS, ImColor(white));
            }
            drawList->AddCircleFilled(p, GRAB_RADIUS - GRAB_BORDER, ImColor(pink));
        }
        
        return changed;
    }
}