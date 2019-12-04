// Copyright 2016 Dolphin Emulator Project
// TODO: Verify what this copyright should be >.>
// Licensed under GPLv2+
// Refer to the license.txt file included.

// This file is a big hack that lets me dump information from Prime every frame.
// For the moment, this is all hard-coded to look for Prime 1 or 2 stuff.
#include <thread>
#include <mutex>
#include <deque>
#include <sstream>
#include <unordered_map>
#include <Common/Assert.h>
#include <Common/Timer.h>
#include <VideoCommon/OnScreenDisplay.h>
#include <VideoCommon/RenderBase.h>
#include "Core/PowerPC/MMU.h"
#include "SFML/Network.hpp"
#include "Common/MsgHandler.h"
#include "Common/FileUtil.h"
#include "PrimeMemoryDumping.h"
#include "../PrimeMemoryDumping/prime1/Prime1JsonDumper.hpp"
#include "../PrimeMemoryDumping/common/json.hpp"

#include "../PrimeMemoryDumping/prime1/actors/CPlayer.hpp"
#include "../PrimeMemoryDumping/prime1/CWorld.hpp"
#include "../PrimeMemoryDumping/prime1/CStateManager.hpp"
#include "../PrimeMemoryDumping/prime1/CGameGlobalObjects.hpp"
#include "../PrimeMemoryDumping/prime1/CGameState.hpp"


using namespace std;
using namespace nlohmann;


namespace PrimeMemoryDumping {
static std::mutex s_active_db_lock;
  struct DolphinCAreaTracking {
    u32 mrea = -1;
    EChain chain = EChain::Deallocated;
    int loadStart = 0;
    int loadEnd = 0;
    EPhase phase = EPhase::LoadHeader;
    EOcclusionState occlusionState = EOcclusionState::Occluded;
    std::vector<int> loadFrames; // Track all load times for each room so we can get average load times.
  };

  static bool initalized = false;

  int timeToFrames(double time) {
    return round(time * 60);
  }

  double veclen(double vec[3]) {
    return sqrt(
      vec[0] * vec[0] +
      vec[1] * vec[1] +
      vec[2] * vec[2]
    );
  }

  static int currentRoom = 0;
  static double currentTime = 0;
  static double currentRoomStart = 0;
  static double lastRoomTime = 0;
  static double speed[3];
  static double rotationalSpeed[3];
  static unordered_map<u32, DolphinCAreaTracking> areas;

  void handleWorldLoads();
  void CalcMessages();
  void ShowMessages();

  void DumpMemoryForFrame() {
    if (!initalized) {
      initalized = true;
      LoadDatabase();
    }

    u32 gameID = PowerPC::HostRead_U32(0x80000000);
    u16 makerID = PowerPC::HostRead_U16(0x80000004);

    json json_message;
    //Prime 1
    if (gameID == 0x474D3845 && makerID == 0x3031) {
      CalcMessages();
      ShowMessages();
      handleWorldLoads();
    }
  }

  void LogRead(u64 offset, u64 len) {

  }

  void ShowMessages() {
    stringstream msg;
    msg << fixed << setprecision(3);
    msg << "Current IGT: " << setw(7) << currentTime << "s/"
        << setw(7) << timeToFrames(currentTime) << " frames" << endl;

    msg << "Current room time: " << setw(7) << (currentTime - currentRoomStart) << "s/"
        << setw(7) << timeToFrames(currentTime - currentRoomStart) << " frames" << endl;

    msg << "Last room time: " << setw(7) << lastRoomTime
        << "s/" << setw(7) << timeToFrames(lastRoomTime) << " frames" << endl;

    msg << "Speed: "
        << setw(7) << speed[0] << "x "
        << setw(7) << speed[1] << "y "
        << setw(7) << speed[2] << "z "
        << setw(7) << sqrt(speed[0] * speed[0] + speed[1] * speed[1]) << " horizontal" << endl;

    msg << "Rotation: "
        << setw(7) << rotationalSpeed[0] << "x "
        << setw(7) << rotationalSpeed[1] << "y "
        << setw(7) << rotationalSpeed[2] << "z "
        << setw(7) << veclen(rotationalSpeed) << " total" << endl;
//    OSD::AddMessage(msg.str(), 1000, 0xFF33FFFF);
//    WARN_LOG(PRIME, "%s", msg.str().c_str());
  }

  void CalcMessages() {
    CGameGlobalObjects global(CGameGlobalObjects::LOCATION);
    CStateManager stateManager(CStateManager::LOCATION);
    CGameState gameState = global.gameState.deref();
    CPlayer player = stateManager.player.deref();
    CPlayerState playerState = stateManager.playerState.deref().deref();
    CWorld world = stateManager.world.deref();

    currentTime = gameState.playTime.read();
    int room = world.currentAreaID.read();
    if (room != currentRoom) {
      currentRoom = room;
      lastRoomTime = currentTime - currentRoomStart;
      currentRoomStart = currentTime;
      WARN_LOG(PRIME, "Room time: %f/%d frames", lastRoomTime, timeToFrames(lastRoomTime));
    }

    speed[0] = player.velocity.x.read();
    speed[1] = player.velocity.y.read();
    speed[2] = player.velocity.z.read();

    rotationalSpeed[0] = player.angularVelocity.x.read();
    rotationalSpeed[1] = player.angularVelocity.y.read();
    rotationalSpeed[2] = player.angularVelocity.z.read();
  }

  string phaseName(EPhase phase) {
    switch (phase) {
      case EPhase::LoadHeader:
        return "Load header";
      case EPhase::LoadSecSizes:
        return "Load section sizes";
      case EPhase::ReserveSections:
        return "Reserve Sections";
      case EPhase::LoadDataSections:
        return "Load data sections";
      case EPhase::WaitForFinish:
        return "Wait for finish";
    }
    return "Unknown";
  }

  string chainName(EChain chain) {
    switch (chain) {
      case EChain::Invalid:
        return "Invalid";
      case EChain::ToDeallocate:
        return "ToDeallocate";
      case EChain::Deallocated:
        return "Deallocated";
      case EChain::Loading:
        return "Loading";
      case EChain::Alive:
        return "Alive";
      case EChain::AliveJudgement:
        return "Alive Judgement";
    }
    return "Unknown";
  }

  string occlusionName(EOcclusionState state) {
    switch (state) {
      case EOcclusionState::Occluded:
        return "Occluded";
      case EOcclusionState::Visible:
        return "Visible";
    }
    return "Unknown";
  }

  void handleWorldLoads() {
    std::lock_guard<std::mutex> codes_lock(s_active_db_lock);
    CGameGlobalObjects global(CGameGlobalObjects::LOCATION);
    CStateManager manager(CStateManager::LOCATION);
    if (!manager.ptr())
        return;

    CGameState gameState = global.gameState.deref();
    double time = gameState.playTime.read();
    int frame = timeToFrames(time);

    CWorld world = manager.world.deref();
    int areaCount = world.areas.size.read();
    for (int i = 0; i < areaCount; i++) {
      auto autoPtr = world.areas[i];
//    cout << hex << " autoptr offset: " << autoPtr.ptr();
//    cout << hex << " autoptr second value: " << autoPtr.dataPtr.read();
//    cout << endl;
      CGameArea area = autoPtr.dataPtr.deref();
      u32 mrea = area.mrea.read();

      DolphinCAreaTracking &areaTracking = areas[mrea];

      EChain newChain = area.curChain.read();
      if (newChain != areaTracking.chain) {
        INFO_LOG(PRIME, "%d: Area %x chain %s -> %s",
                 frame,
                 mrea,
                 chainName(areaTracking.chain).c_str(),
                 chainName(newChain).c_str());
        areaTracking.chain = newChain;
        if (newChain == EChain::Loading) {
          areaTracking.loadStart = frame;
        }
        if (newChain == EChain::Alive) {
          areaTracking.loadEnd = frame;
          int loadFrames = areaTracking.loadEnd - areaTracking.loadStart;
          areaTracking.loadFrames.push_back(loadFrames);
          WARN_LOG(PRIME, "%d: Area %x loaded in %d frames",
            frame,
            mrea,
            loadFrames
            );
        }
      }

      EPhase newPhase = area.phase.read();
      if (newPhase != areaTracking.phase) {
        INFO_LOG(PRIME, "%d: Area %x phase %s -> %s",
                 frame,
                 mrea,
                 phaseName(areaTracking.phase).c_str(),
                 phaseName(newPhase).c_str());
        areaTracking.phase = newPhase;
      }

      EOcclusionState newOcclusion = EOcclusionState::Occluded;
      if (area.postConstructed.read() != 0) {
        CPostConstructed pc = area.postConstructed.read();
        newOcclusion = pc.occlusionState.read();
      }

      if (newOcclusion != areaTracking.occlusionState) {
        INFO_LOG(PRIME, "%d: Area %x occlusion %s -> %s",
                 frame,
                 mrea,
                 occlusionName(areaTracking.occlusionState).c_str(),
                 occlusionName(newOcclusion).c_str());
        areaTracking.occlusionState = newOcclusion;
      }
    }
  }

  constexpr std::string_view db_name = "PrimeLoadDatabase.dat";
  void LoadDatabase() {
      std::lock_guard<std::mutex> codes_lock(s_active_db_lock);
      INFO_LOG(PRIME, "Loading Area Load Database");
      areas.clear();
      std::string db_path = File::GetUserPath(D_DUMP_IDX) + db_name.data();
      FILE* file = fopen(db_path.c_str(), "rb");
      if (!file)
          return;

      int areaCount;
      fread(&areaCount, 1, sizeof(int), file);
      for (int i = 0; i < areaCount; ++i) {
          int areaId;
          fread(&areaId, 1, sizeof(int), file);
          areas[areaId].mrea = areaId;
          int loadCount;
          fread(&loadCount, 1, sizeof(int), file);
          for (int j = 0; j < loadCount; ++j) {
              int frameCount;
              fread(&frameCount, 1, sizeof(int), file);
              areas[areaId].loadFrames.push_back(frameCount);
          }
      }
      fclose(file);
  }
  void SaveDatabase() {
      std::lock_guard<std::mutex> codes_lock(s_active_db_lock);
      INFO_LOG(PRIME, "Saving Area Load Database");
      std::string db_path = File::GetUserPath(D_DUMP_IDX) + "~" + db_name.data();
      FILE* file = fopen(db_path.c_str(), "wb");
      int len = areas.size();
      fwrite(&len, 1, sizeof(int), file);
      for (const auto& [id, area] : areas) {
          fwrite(&id, 1, sizeof(int), file);
          size_t loadFrameCount = area.loadFrames.size();
          fwrite(&loadFrameCount, 1, sizeof(int), file);
          for (const auto& frames : area.loadFrames)
              fwrite(&frames, 1, sizeof(int), file);
      }

      fclose(file);
      File::Rename(db_path, File::GetUserPath(D_DUMP_IDX) + db_name.data());
  }
}
