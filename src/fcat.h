#pragma once
#ifndef MANGOHUD_FCAT_H
#define MANGOHUD_FCAT_H

#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <array>

#include "timing.hpp"

#include "overlay_params.h"
#include "overlay.h"

struct fcatoverlay{
  const struct overlay_params* params = nullptr;
  const std::array<const ImColor,16> sequence={{{255, 255, 255},{0, 255, 0},{0, 0, 255},{255, 0, 0},{0, 128, 128},{0, 0, 128},{0, 128, 0},{0, 255, 255},{128, 0, 0},{192, 192, 192},{128, 0, 128},{128, 128, 0},{128, 128, 128},{255, 0, 255},{255, 255, 0},{255, 128, 0}}};
  void update(const struct overlay_params* params_){
    params=params_;
  };
  ImColor get_next_color (const swapchain_stats& sw_stats){
    size_t currentColor = sw_stats.n_frames % 16;// should probably be sequence.size(); but this doesn't matter as all FCAT analysis tools use this exact 16 colour sequence.
    ImColor output = sequence[currentColor];
    return output;
  };
  std::array<ImVec2,3> get_overlay_corners()
  {
    unsigned short screen_edge=params->fcat_screen_edge;
    auto window_size = ImVec2(params->fcat_overlay_width,ImGui::GetIO().DisplaySize.y);
    auto p_min = ImVec2(0.,0.);
    auto p_max = ImVec2(window_size.x,ImGui::GetIO().DisplaySize.y);
    //Switch the used screen edge, this enables capture from devices with any screen orientation.
    //This goes counter-clockwise from the left edge (0)
    switch (screen_edge)
      {
      default:
      case 0:
	break;
      case 1:
	window_size = ImVec2(ImGui::GetIO().DisplaySize.x,window_size.x);
	p_min = ImVec2(0,ImGui::GetIO().DisplaySize.y - window_size.y);
	p_max = ImVec2(ImGui::GetIO().DisplaySize.x,ImGui::GetIO().DisplaySize.y);
	break;
      case 2:
	window_size = ImVec2(window_size.x,ImGui::GetIO().DisplaySize.y);
	p_min = ImVec2(ImGui::GetIO().DisplaySize.x-window_size.x,0);
	p_max = ImVec2(ImGui::GetIO().DisplaySize.x,ImGui::GetIO().DisplaySize.y);
	break;
      case 3:
	window_size = ImVec2(ImGui::GetIO().DisplaySize.x,window_size.x);
	p_min = ImVec2(0,0);
	p_max = ImVec2(ImGui::GetIO().DisplaySize.x,window_size.y);
	break;
         }
    std::array<ImVec2,3> output={{p_min,p_max,window_size}};
    return output;
  };
};

#endif
