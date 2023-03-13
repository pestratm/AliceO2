// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
#include <iostream>
#include <sstream>
#include "DataFormatsFOCAL/ErrorHandling.h"

using namespace o2::focal;

IndexExceptionEvent::IndexExceptionEvent(unsigned int index, unsigned int maxindex, IndexType_t source)
{
  std::string nameIndexType;
  switch (source) {
    case IndexType_t::PAD_CHANNEL:
      nameIndexType = "channel";
      break;
    case IndexType_t::PAD_LAYER:
      nameIndexType = "pad layer";
      break;
    case IndexType_t::PAD_NHALVES:
      nameIndexType = "pad half stave";
      break;
    case IndexType_t::PIXEL_LAYER:
      nameIndexType = "pixel layer";
      break;
    case IndexType_t::TRIGGER_WINDOW:
      nameIndexType = "trigger window";
    default:
      break;
  }
  mMessageBuffer = "Accessing invalid entry of type " + nameIndexType + ": " + std::to_string(index) + " ( max " + std::to_string(maxindex) + ")";
}

const char* IndexExceptionEvent::what() const noexcept
{
  return mMessageBuffer.data();
}

std::ostream& o2::focal::operator<<(std::ostream& stream, const o2::focal::IndexExceptionEvent& error)
{
  stream << error.what();
  return stream;
}