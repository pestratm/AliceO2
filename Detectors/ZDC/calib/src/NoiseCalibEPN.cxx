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

#include <TROOT.h>
#include <TFile.h>
#include <TH1.h>
#include <TString.h>
#include <TStyle.h>
#include <TDirectory.h>
#include "ZDCCalib/CalibParamZDC.h"
#include "ZDCCalib/NoiseCalibEPN.h"
#include "Framework/Logger.h"

using namespace o2::zdc;

int NoiseCalibEPN::init()
{
  if (mVerbosity > DbgZero) {
    mModuleConfig->print();
  }

  // Inspect reconstruction parameters
  o2::zdc::RecoParamZDC& ropt = const_cast<o2::zdc::RecoParamZDC&>(RecoParamZDC::Instance());
  ropt.print();
  mRopt = (o2::zdc::RecoParamZDC*)&ropt;

  // Inspect calibration parameters
  const auto& opt = CalibParamZDC::Instance();
  opt.print();
  if (opt.debugOutput == true) {
    setSaveDebugHistos();
  }

  for (int isig = 0; isig < NChannels; isig++) {
    if (mH[isig] == nullptr) {
      mH[isig] = new o2::dataformats::FlatHisto1D<double>(4096, -2048.5, 2047.5);
    } else {
      mH[isig]->clear();
    }
    if (mSaveDebugHistos) {
      int nbx = 4096 * NTimeBinsPerBC - NTimeBinsPerBC + 1;
      double xmin = -2048 * NTimeBinsPerBC - 0.5;
      double xmax = 2047 * NTimeBinsPerBC + 0.5;
      if (mHS[isig] == nullptr) {
        mHS[isig] = new o2::dataformats::FlatHisto1D<double>(nbx, xmin, xmax);
      } else {
        mHS[isig]->clear();
      }
      if (mHD[isig] == nullptr) {
        mHD[isig] = new o2::dataformats::FlatHisto1D<double>(nbx, xmin / double(NTimeBinsPerBC), xmax / double(NTimeBinsPerBC));
      } else {
        mHD[isig]->clear();
      }
    }
  }

  // Fill maps to decode the pattern of channels with hit
  for (int ich = 0; ich < NChannels; ich++) {
    // If the reconstruction parameters were not manually set
    if (ropt.amod[ich] < 0 || ropt.ach[ich] < 0) {
      for (int im = 0; im < NModules; im++) {
        for (uint32_t ic = 0; ic < NChPerModule; ic++) {
          if (mModuleConfig->modules[im].channelID[ic] == ich && mModuleConfig->modules[im].readChannel[ic]) {
            ropt.amod[ich] = im;
            ropt.ach[ich] = ic;
            // Fill mask to identify all channels
            mChMask[ich] = (0x1 << (4 * im + ic));
            goto next_ich;
          }
        }
      }
    }
  next_ich:;
    if (mVerbosity > DbgZero) {
      LOG(info) << "Channel " << ich << "(" << ChannelNames[ich] << ") mod " << ropt.amod[ich] << " ch " << ropt.ach[ich];
    }
  }

  mInitDone = true;
  return 0;
}

//______________________________________________________________________________
int NoiseCalibEPN::process(const gsl::span<const o2::zdc::OrbitData>& orbitData, const gsl::span<const o2::zdc::BCData>& bcdata, const gsl::span<const o2::zdc::ChannelData>& chdata)
{
  if (!mInitDone) {
    init();
  }

  // Prepare orbit information only if we need to fill debug histograms
  std::map<uint32_t, int> orbit; // Orbit map for fast access
  float offset[NChannels];       // Offset in current orbit
  for (int ich = 0; ich < NChannels; ich++) {
    offset[ich] = std::numeric_limits<float>::infinity();
  }
  if (mSaveDebugHistos) {
    int norb = orbitData.size();
    if (mVerbosity >= DbgFull) {
      LOG(info) << "Dump of pedestal data lookup table";
    }
    for (int iorb = 0; iorb < norb; iorb++) {
      orbit[orbitData[iorb].ir.orbit] = iorb;
      if (mVerbosity >= DbgFull) {
        LOG(info) << "orbitData[" << orbitData[iorb].ir.orbit << "] = " << iorb;
      }
    }
  }

  auto nbc = bcdata.size();
  for (int ibc = 1; ibc < nbc; ibc++) {
    auto& bcp = bcdata[ibc - 1];
    auto& bcc = bcdata[ibc];
    if (bcc.ir.bc != 0 || bcp.ir.bc != 3563 || (bcp.ir.orbit + 1) != bcc.ir.orbit) {
      continue;
    }
    auto chEnt = bcc.ref.getFirstEntry();
    auto nch = bcc.ref.getEntries();
    // Extract pedestal information only if we need to fill debug histograms
    if (mSaveDebugHistos) {
      std::map<uint32_t, int>::iterator it = orbit.find(bcc.ir.orbit);
      if (it != orbit.end()) {
        auto& orbitdata = orbitData[it->second];
        for (int ich = 0; ich < NChannels; ich++) {
          auto myped = float(orbitdata.data[ich]) * mModuleConfig->baselineFactor;
          if (myped >= ADCMin && myped <= ADCMax) {
            // Pedestal information is present for this channel
            offset[ich] = myped;
          } else {
            offset[ich] = std::numeric_limits<float>::infinity();
          }
        }
      } else {
        for (int ich = 0; ich < NChannels; ich++) {
          offset[ich] = std::numeric_limits<float>::infinity();
        }
      }
    }

    for (int ich = 0; ich < nch; ich++) {
      const auto& chd = chdata[chEnt++];
      if (chd.id < NChannels) {
        // Check trigger flags
        ModuleTriggerMapData mtc, mtp;
        mtp.w = bcp.moduleTriggers[mRopt->amod[chd.id]];
        mtc.w = bcc.moduleTriggers[mRopt->amod[chd.id]];
        if (mtp.f.Auto_m                                                         // Auto trigger in bunch -2
            || mtp.f.Auto_0 || mtp.f.Alice_0 || (bcp.triggers & mChMask[chd.id]) // Trigger or hit in bunch -1
            || mtc.f.Auto_0 || mtc.f.Alice_0 || (bcc.triggers & mChMask[chd.id]) // Trigger or hit in bunch -2
            || mtc.f.Auto_1 || mtc.f.Alice_1                                     // Trigger in bunch +1
        ) {
#ifdef O2_ZDC_DEBUG
          printf("%u.%04u SKIP %s%s%s%s%s%s%s%s%s\n",
                 mtp.f.Auto_m ? "p.Auto_m" : "",
                 mtp.f.Auto_0 ? "p.Auto_0" : "",
                 mtp.f.Alice_0 ? "p.Alice_0" : "",
                 (bcp.triggers & mChMask[chd.id]) ? "p.HIT" : "",
                 mtc.f.Auto_0 ? "c.Auto_0" : "",
                 mtc.f.Alice_0 ? "c.Alice_0" : "",
                 (bcc.triggers & mChMask[chd.id]) ? "c.HIT" : "",
                 mtc.f.Auto_1 ? "c.Auto_1" : "",
                 mtc.f.Alice_1 ? "c.Alice_1" : "");
#endif
          continue;
        }
        int ss = 0;
        int sq = 0;
        for (int is = 0; is < NTimeBinsPerBC; is++) {
          auto s = chd.data[is];
          mH[chd.id]->fill(s);
          ss += s;
          sq += s * s;
        }
        int v = NTimeBinsPerBC * sq - ss * ss;
        if (v > 0) {
          // This should always be the case
          mData.addEntry(chd.id, v);
        }
        // Debug histograms
        if (mSaveDebugHistos && chd.id < NChannels) {
          mHS[chd.id]->fill(ss);
          mHD[chd.id]->fill(ss / double(NTimeBinsPerBC) - offset[chd.id]);
        }
      }
    }
  }
  return 0;
}

//______________________________________________________________________________
int NoiseCalibEPN::endOfRun()
{
  if (mVerbosity > DbgZero) {
    mData.print();
  }
  if (mSaveDebugHistos) {
    saveDebugHistos();
  }
  return 0;
}

//______________________________________________________________________________
int NoiseCalibEPN::saveDebugHistos(const std::string fn)
{
  LOG(info) << "Saving debug histograms on file " << fn;
  int ierr = mData.saveDebugHistos(fn);
  if (ierr != 0) {
    return ierr;
  }
  TDirectory* cwd = gDirectory;
  TFile* f = new TFile(fn.data(), "update");
  if (f->IsZombie()) {
    LOG(error) << "Cannot update file: " << fn;
    return 1;
  }
  for (int32_t is = 0; is < NChannels; is++) {
    auto p = mH[is]->createTH1F(TString::Format("hs%d", is).Data());
    p->SetTitle(TString::Format("Baseline samples %s", ChannelNames[is].data()));
    p->Write("", TObject::kOverwrite);
  }
  for (int32_t is = 0; is < NChannels; is++) {
    auto p = mHS[is]->createTH1F(TString::Format("hss%d", is).Data());
    p->SetTitle(TString::Format("Bunch sum of samples %s", ChannelNames[is].data()));
    p->Write("", TObject::kOverwrite);
  }
  for (int32_t is = 0; is < NChannels; is++) {
    auto p = mHD[is]->createTH1F(TString::Format("hsd%d", is).Data());
    p->SetTitle(TString::Format("Baseline estimation difference %s", ChannelNames[is].data()));
    p->Write("", TObject::kOverwrite);
  }
  f->Close();
  cwd->cd();
  return 0;
}
