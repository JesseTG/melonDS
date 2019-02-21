/*
    Copyright 2016-2019 Arisotura

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef LAN_H
#define LAN_H

#include "../types.h"

namespace LAN
{

typedef struct
{
    char DeviceName[128];
    char FriendlyName[128];
    char Description[128];
    u8 MAC[6];
    u8 IP_v4[4];
    u8 DNS[8][4];

    void* Internal;

} AdapterData;


extern AdapterData* Adapters;
extern int NumAdapters;


bool Init();
void DeInit();

}

#endif // LAN_H