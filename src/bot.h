// IJ2FrameLab - scripted test inputs ("bot"): lets the tool drive P1 with XInput so moves can be
// tested without a person at the controller. Commands are read from IJ2FrameLab_cmd.txt next to the mod.
#pragma once
#include <cstdint>
#include <string>
#include "ij2.h"

namespace bot {
void PollCommandFile(const std::string& dir);   // render thread, throttled
void OnSimFrame(const ij2::Snapshot& sn);       // once per simulation frame
void OnRenderFrame();                           // every rendered frame (real-time steps)
bool Running();
std::string Status();
// read by the XInput hook
bool Override(uint16_t& buttons);
}
