#pragma once

#include <rex/ui/imgui_dialog.h>
#include <rex/cvar.h>
#include <rex/logging/macros.h>

#include <imgui.h>
#include <cmath>
#include <algorithm>

REXCVAR_DEFINE_BOOL(show_touch_controls, false, "Input",
                    "Display on-screen virtual touch controls");

REXCVAR_DEFINE_DOUBLE(touch_controls_opacity, 0.65, "Input",
                      "Opacity for virtual touch controls (0.1 - 1.0)");

REXCVAR_DEFINE_DOUBLE(touch_stick_radius, 80.0, "Input",
                      "Radius in pixels for virtual analog sticks");

class TouchOverlayDialog : public rex::ui::ImGuiDialog {
 public:
  explicit TouchOverlayDialog(rex::ui::ImGuiDrawer* drawer)
      : rex::ui::ImGuiDialog(drawer) {}

  struct TouchState {
    float lstick_x = 0.0f;
    float lstick_y = 0.0f;
    float rstick_x = 0.0f;
    float rstick_y = 0.0f;

    bool btn_a = false;
    bool btn_b = false;
    bool btn_x = false;
    bool btn_y = false;
    bool btn_lb = false;
    bool btn_rb = false;
    bool btn_lt = false;
    bool btn_rt = false;
    bool btn_start = false;
    bool btn_back = false;
  };

  static TouchState& GetCurrentTouchState() {
    static TouchState state;
    return state;
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!REXCVAR_GET(show_touch_controls)) {
      return;
    }

    float opacity = static_cast<float>(REXCVAR_GET(touch_controls_opacity));
    opacity = std::clamp(opacity, 0.1f, 1.0f);

    float stick_radius = static_cast<float>(REXCVAR_GET(touch_stick_radius));
    stick_radius = std::clamp(stick_radius, 40.0f, 150.0f);

    ImVec2 screen_size = io.DisplaySize;
    if (screen_size.x <= 0.0f || screen_size.y <= 0.0f) {
      return;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(screen_size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBackground;

    bool open = true;
    if (!ImGui::Begin("##TouchControlsOverlay", &open, flags)) {
      ImGui::End();
      return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 bg_color = ImColor(0.12f, 0.12f, 0.14f, 0.5f * opacity);
    ImU32 active_color = ImColor(0.85f, 0.2f, 0.2f, 0.8f * opacity);
    ImU32 outline_color = ImColor(0.9f, 0.9f, 0.9f, 0.7f * opacity);
    ImU32 text_color = ImColor(1.0f, 1.0f, 1.0f, 0.9f * opacity);

    auto& state = GetCurrentTouchState();

    // 1. Left Stick (Movement - WASD / Left Thumbstick)
    ImVec2 lstick_center(stick_radius + 40.0f, screen_size.y - stick_radius - 50.0f);
    draw_list->AddCircleFilled(lstick_center, stick_radius, bg_color, 32);
    draw_list->AddCircle(lstick_center, stick_radius, outline_color, 32, 2.5f);

    // Track touch in left stick area
    ImVec2 lstick_knob = lstick_center;
    state.lstick_x = 0.0f;
    state.lstick_y = 0.0f;

    if (io.MouseDown[0]) {
      ImVec2 touch_pos = io.MousePos;
      float dx = touch_pos.x - lstick_center.x;
      float dy = touch_pos.y - lstick_center.y;
      float dist = std::sqrt(dx * dx + dy * dy);

      if (dist <= stick_radius * 1.6f && touch_pos.x < screen_size.x * 0.45f) {
        float norm_dist = std::min(dist, stick_radius);
        float angle = std::atan2(dy, dx);
        lstick_knob = ImVec2(lstick_center.x + std::cos(angle) * norm_dist,
                             lstick_center.y + std::sin(angle) * norm_dist);
        state.lstick_x = (dx / stick_radius);
        state.lstick_y = -(dy / stick_radius); // Up is positive in gamepads
        state.lstick_x = std::clamp(state.lstick_x, -1.0f, 1.0f);
        state.lstick_y = std::clamp(state.lstick_y, -1.0f, 1.0f);
      }
    }

    draw_list->AddCircleFilled(lstick_knob, stick_radius * 0.38f, active_color, 24);
    draw_list->AddCircle(lstick_knob, stick_radius * 0.38f, outline_color, 24, 2.0f);

    // 2. Action Buttons (Right side: X, Y, B, A in Xbox diamond layout)
    float btn_radius = stick_radius * 0.36f;
    ImVec2 cluster_center(screen_size.x - stick_radius - 80.0f,
                          screen_size.y - stick_radius - 50.0f);
    float diamond_offset = stick_radius * 0.72f;

    auto DrawButton = [&](ImVec2 pos, const char* label, bool& pressed) {
      bool is_hovered = false;
      if (io.MouseDown[0]) {
        float dx = io.MousePos.x - pos.x;
        float dy = io.MousePos.y - pos.y;
        if (std::sqrt(dx * dx + dy * dy) <= btn_radius * 1.25f) {
          is_hovered = true;
        }
      }
      pressed = is_hovered;

      ImU32 fill = pressed ? active_color : bg_color;
      draw_list->AddCircleFilled(pos, btn_radius, fill, 24);
      draw_list->AddCircle(pos, btn_radius, outline_color, 24, 2.0f);

      ImVec2 text_size = ImGui::CalcTextSize(label);
      ImVec2 text_pos(pos.x - text_size.x * 0.5f, pos.y - text_size.y * 0.5f);
      draw_list->AddText(text_pos, text_color, label);
    };

    // A (Bottom - Jump), Y (Top - Heavy), X (Left - Light), B (Right - Cross)
    ImVec2 pos_a(cluster_center.x, cluster_center.y + diamond_offset);
    ImVec2 pos_y(cluster_center.x, cluster_center.y - diamond_offset);
    ImVec2 pos_x(cluster_center.x - diamond_offset, cluster_center.y);
    ImVec2 pos_b(cluster_center.x + diamond_offset, cluster_center.y);

    DrawButton(pos_a, "A", state.btn_a);
    DrawButton(pos_b, "B", state.btn_b);
    DrawButton(pos_x, "X", state.btn_x);
    DrawButton(pos_y, "Y", state.btn_y);

    // 3. Shoulder Buttons & Triggers (Top corners)
    float shoulder_w = stick_radius * 0.9f;
    float shoulder_h = stick_radius * 0.42f;

    auto DrawPillButton = [&](ImVec2 pos, ImVec2 size, const char* label, bool& pressed) {
      ImVec2 min_pt = pos;
      ImVec2 max_pt = ImVec2(pos.x + size.x, pos.y + size.y);

      bool is_hovered = false;
      if (io.MouseDown[0]) {
        if (io.MousePos.x >= min_pt.x && io.MousePos.x <= max_pt.x &&
            io.MousePos.y >= min_pt.y && io.MousePos.y <= max_pt.y) {
          is_hovered = true;
        }
      }
      pressed = is_hovered;

      ImU32 fill = pressed ? active_color : bg_color;
      draw_list->AddRectFilled(min_pt, max_pt, fill, 8.0f);
      draw_list->AddRect(min_pt, max_pt, outline_color, 8.0f, 0, 2.0f);

      ImVec2 text_size = ImGui::CalcTextSize(label);
      ImVec2 text_pos(min_pt.x + (size.x - text_size.x) * 0.5f,
                      min_pt.y + (size.y - text_size.y) * 0.5f);
      draw_list->AddText(text_pos, text_color, label);
    };

    // Left side: LT (Block), LB (Magic)
    DrawPillButton(ImVec2(30.0f, 40.0f), ImVec2(shoulder_w, shoulder_h), "LT", state.btn_lt);
    DrawPillButton(ImVec2(40.0f + shoulder_w, 40.0f), ImVec2(shoulder_w, shoulder_h), "LB", state.btn_lb);

    // Right side: RT (Grab), RB (Interact)
    DrawPillButton(ImVec2(screen_size.x - 40.0f - shoulder_w * 2.0f, 40.0f),
                   ImVec2(shoulder_w, shoulder_h), "RB", state.btn_rb);
    DrawPillButton(ImVec2(screen_size.x - 30.0f - shoulder_w, 40.0f),
                   ImVec2(shoulder_w, shoulder_h), "RT", state.btn_rt);

    // 4. System Buttons: Back / Options, Start / Menu
    DrawButton(ImVec2(screen_size.x * 0.38f, 40.0f), "BACK", state.btn_back);
    DrawButton(ImVec2(screen_size.x * 0.62f, 40.0f), "START", state.btn_start);

    ImGui::End();
  }
};
