// Copyright 2016 Dolphin Emulator Project
// TODO: Verify what this copyright should be >.>
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#define PRIME_DUMP_JSON true

#include "Common/CommonTypes.h"

namespace PrimeMemoryDumping {
    void LoadDatabase();
    void SaveDatabase();
    void DumpMemoryForFrame();
    void NetworkThread();
    void LogRead(u64 offset, u64 len);
};
