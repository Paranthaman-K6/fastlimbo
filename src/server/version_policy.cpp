// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#include "server/version_policy.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>

#include "protocol/versions.h"

namespace limbo::server {

namespace {

const std::map<std::string, int>& labelTable() {
  static const std::map<std::string, int> kLabels = {
      {"1.8.x", 47},    {"1.12", 335},   {"1.12.1", 338},  {"1.12.2", 340},
      {"1.13", 393},    {"1.13.1", 401},  {"1.13.2", 404},  {"1.14", 477},
      {"1.14.1", 480},  {"1.14.2", 485},  {"1.14.3", 490},  {"1.14.4", 498},
      {"1.15", 573},    {"1.15.1", 575},  {"1.15.2", 578},  {"1.16", 735},
      {"1.16.1", 736},  {"1.16.2", 751},  {"1.16.3", 753},  {"1.16.4", 754},
      {"1.16.5", 754},  {"1.17", 755},    {"1.17.1", 756},  {"1.18", 757},
      {"1.18.1", 757},  {"1.18.2", 758},  {"1.19", 759},    {"1.19.2", 760},
      {"1.19.3", 761},  {"1.19.4", 762},  {"1.20", 763},    {"1.20.1", 763},
      {"1.20.2", 764},  {"1.20.3", 765},  {"1.20.4", 765},  {"1.20.5", 766},
      {"1.20.6", 766},  {"1.21", 767},    {"1.21.1", 767},  {"1.21.2", 768},
      {"1.21.3", 768},  {"1.21.4", 769},  {"1.21.5", 770},  {"1.21.6", 771},
      {"1.21.7", 772},  {"1.21.8", 772},  {"1.21.9", 773},  {"1.21.10", 773},
      {"1.21.11", 774}, {"26.1", 775},    {"26.2", 776},    {"26.3", 777},
  };
  return kLabels;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool isDecimal(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

void addPvn(Policy& pol, long pvn, const std::string& item) {
  if (pvn < 47 || pvn > limbo::proto::maxSupportedPvn()) {
    pol.warnings.push_back("PVN " + item + " outside supported range 47.." +
                           std::to_string(limbo::proto::maxSupportedPvn()) + "; ignored");
    return;
  }
  pol.allowed.insert(static_cast<int>(pvn));
}

}  // namespace

Policy parseAllowedVersions(const std::string& spec) {
  Policy pol;
  std::string trimmed = trim(spec);
  bool sawAll = false;
  bool sawItem = false;

  if (trimmed.empty()) {
    sawAll = true;
  } else {
    std::stringstream ss(trimmed);
    std::string part;
    while (std::getline(ss, part, ',')) {
      std::string item = trim(part);
      if (item.empty()) continue;
      sawItem = true;
      if (item == "all") {
        sawAll = true;
        continue;
      }
      size_t dash = item.find('-');
      if (dash != std::string::npos) {
        std::string lo = trim(item.substr(0, dash));
        std::string hi = trim(item.substr(dash + 1));
        if (!isDecimal(lo) || !isDecimal(hi)) {
          pol.warnings.push_back("malformed range '" + item + "'; ignored");
          continue;
        }
        long a = std::atol(lo.c_str());
        long b = std::atol(hi.c_str());
        if (a > b) {
          pol.warnings.push_back("reversed range '" + item + "' (low must be <= high); ignored");
          continue;
        }
        bool any = false;
        for (long p = a; p <= b; ++p) {
          if (p >= 47 && p <= limbo::proto::maxSupportedPvn()) {
            pol.allowed.insert(static_cast<int>(p));
            any = true;
          }
        }
        if (!any) {
          pol.warnings.push_back("range '" + item + "' matches no supported PVN; ignored");
        }
        continue;
      }
      if (isDecimal(item)) {
        addPvn(pol, std::atol(item.c_str()), item);
        continue;
      }
      auto it = labelTable().find(item);
      if (it != labelTable().end()) {
        pol.allowed.insert(it->second);
        continue;
      }
      pol.warnings.push_back("unknown version label '" + item + "'; ignored");
    }
    if (!sawItem && !sawAll) {
      // e.g. spec was only commas/whitespace
      sawAll = true;
    }
  }

  if (sawAll) {
    for (int p = 47; p <= limbo::proto::maxSupportedPvn(); ++p) {
      if (limbo::proto::eraForPvn(p) != limbo::proto::Era::UNKNOWN) {
        pol.allowed.insert(p);
      }
    }
  }
  return pol;
}

bool isAllowed(const Policy& pol, int pvn) {
  if (limbo::proto::eraForPvn(pvn) == limbo::proto::Era::UNKNOWN) return false;
  if (pvn > limbo::proto::maxSupportedPvn()) return false;
  return pol.allowed.count(pvn) > 0;
}

std::string summarize(const Policy& pol) {
  std::ostringstream out;
  out << pol.allowed.size() << " allowed PVN(s)";
  if (!pol.allowed.empty()) {
    out << ": ";
    bool first = true;
    for (int p : pol.allowed) {
      if (!first) out << ',';
      out << p;
      first = false;
    }
  }
  if (!pol.warnings.empty()) {
    out << " [" << pol.warnings.size() << " warning(s)]";
  }
  return out.str();
}

}  // namespace limbo::server
