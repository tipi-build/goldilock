// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary
#pragma once

#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/filesystem.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/predef.h>
#include <boost/lexical_cast.hpp>
#include <boost/process.hpp>
#include <boost/process/handles.hpp>
#include <boost/regex.hpp>
#include <boost/scope_exit.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

#include <goldilock/file.hpp>
#include <goldilock/fstream.hpp>
#include <goldilock/string.hpp>

namespace tipi::goldilock {
  
  namespace fs = boost::filesystem;
  namespace bp = boost::process;
  using namespace std::string_literals;
  using namespace std::chrono_literals;

  inline std::string get_random_uuid() {
    static boost::uuids::random_generator uuid_gen;
    return boost::lexical_cast<std::string>(uuid_gen());
  }

  //!\brief get the numerial index suffixed to a lockfile from its filename
  inline std::optional<size_t> extract_lockfile_spot_index(const fs::path& lockfile, const fs::path& p) {
    static std::map<fs::path, boost::regex> rx_cache;

    if(rx_cache.find(lockfile) == rx_cache.end()) {
      const boost::regex esc("[.^$|()\\[\\]{}*+?\\\\]");
      const std::string rep("\\\\&");

      std::string lockfile_name = lockfile.filename().generic_string();
      std::string lockfile_name_rx_str = regex_replace(lockfile_name, esc, rep, boost::match_default | boost::format_sed);
      lockfile_name_rx_str += + "\\.(?<ix>[[:digit:]]+)$";
      rx_cache[lockfile] = boost::regex(lockfile_name_rx_str);
    }

    std::optional<size_t> result;

    boost::smatch matches;
    std::string fullpath = p.filename().generic_string();
    if(boost::regex_match(fullpath, matches, rx_cache.at(lockfile))) {
      result = boost::lexical_cast<size_t>(matches["ix"].str());     
    }

    return result;
  }

  // forward decl
  struct goldilock_spot;  
  std::map<fs::path, goldilock_spot> list_lockfile_spots(const fs::path& lockfile_path);

  struct goldilock_spot {     

    goldilock_spot(const fs::path& lockfile_path)
      : lockfile_{lockfile_path}
      , owned_{true}      
      , guid_{get_random_uuid()}
      , spot_index_{0}
    {
      get_in_line();      
    }

    //!\brief get a new spot in line and update the spot_index
    size_t get_in_line() {
      if(!owned_) {
        throw std::runtime_error("Cannot update someone else's lockfile: "s + lockfile_.generic_string());
      }

      if(current_spot_file_ && fs::exists(current_spot_file_.value())) {
        fs::remove(current_spot_file_.value());
        current_spot_file_.reset();
      }

      bool got_spot = false;
      while(!got_spot) {

        // try to get to own the spot
        auto spots = list_lockfile_spots(lockfile_);

        auto max_spot_it = std::max_element(
          spots.begin(),
          spots.end(),
          [](const auto& a, const auto& b) { 
            return a.second.get_spot_index() < b.second.get_spot_index(); 
          }
        );

        if(max_spot_it != spots.end()) {
          spot_index_ = max_spot_it->second.spot_index_ + 1;
        }

        auto now = std::chrono::system_clock::now();
        timestamp_ = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        fs::path spot_path = get_spot_path();

        {
          auto lockfile_stream = exclusive_fstream::open(spot_path, "wx");
          if(lockfile_stream.is_open()) {
            boost::archive::text_oarchive oa(lockfile_stream);
            oa << *this;

            lockfile_stream.close();
          }
        }

        // now read and see if the contents are as expected
        {
          auto read_back = goldilock_spot::try_read_from(spot_path, lockfile_);
          got_spot = (read_back.has_value() && read_back->get_guid() == get_guid() && read_back->get_timestamp() ==  get_timestamp());
        }

        if(got_spot) {
          current_spot_file_ = spot_path;
        }
      }

      return spot_index_;
    }

    ~goldilock_spot() {
      // expire this spot
      if(owned_ && current_spot_file_ && fs::exists(get_spot_path())) {
        boost::system::error_code fsec;
        fs::remove(get_spot_path(), fsec); // doesn't throw / fail silently
      }
    }

    void update_spot() {
      if(!owned_) {
        throw std::runtime_error("Cannot update someone else's lockfile: "s + lockfile_.generic_string());
      }

      auto now = std::chrono::system_clock::now();
      timestamp_ = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
      std::ofstream ofs(get_spot_path().generic_string());
      boost::archive::text_oarchive oa(ofs);
      oa << *this;
    }

    static goldilock_spot read_from(const fs::path& spot_on_disk, const fs::path& lockfile_path) {
      goldilock_spot result;

      // boost::serialization
      std::ifstream ifs(spot_on_disk.generic_string());
      boost::archive::text_iarchive ia(ifs);
      ia >> result;

      result.lockfile_ = fs::weakly_canonical(lockfile_path);
      result.owned_ = false;
      result.spot_index_ = extract_lockfile_spot_index(result.lockfile_, spot_on_disk).value();      
      return result;
    }

    static std::optional<goldilock_spot> try_read_from(const fs::path& spot_on_disk, const fs::path& lockfile_path) {
      try {
        return read_from(spot_on_disk, lockfile_path);
      }
      catch(...) {
        //
      }

      return std::nullopt;
    }

    bool is_first_in_line() const {
      // try to get to own the spot
      auto spots = list_lockfile_spots(lockfile_);

      auto min_spot_it = std::min_element(
        spots.begin(),
        spots.end(),
        [](const auto& a, const auto& b) { 
          return a.second.get_spot_index() < b.second.get_spot_index(); 
        }
      );

      if(min_spot_it != spots.end()) {
        return min_spot_it->second.guid_ == guid_;
      }

      return false;
    }

    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
      ar &timestamp_;
      ar &guid_;
    }

    fs::path get_spot_path() const {
      if(current_spot_file_) {
        return current_spot_file_.value();
      }

      auto parent_folder = lockfile_.parent_path();
      auto filename = lockfile_.filename().generic_string();
      
      return parent_folder / (filename + "."s + std::to_string(spot_index_));
    }

    fs::path get_lockfile_path() const {
      return lockfile_;
    }

    size_t get_spot_index() const {
      return spot_index_;
    }

    std::string get_guid() {
      return guid_;
    }

    //!\brief our own or someone else's?
    bool is_owned() const {
      return owned_;
    }

    //!\brief is the spot expired
    size_t get_timestamp() const {
      return timestamp_;
    }

    bool is_valid(uint16_t lifetime_seconds = 60) const {
      auto end_of_validity = timestamp_ + lifetime_seconds; // expires after 60s
      auto now = std::chrono::system_clock::now();
      auto unix_ts_now = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
      return end_of_validity >= unix_ts_now;
    }

    bool is_expired(uint16_t lifetime_seconds = 60) const {
      return !is_valid(lifetime_seconds);
    }

  private:
    goldilock_spot() { /* for deserialization */ }
    //!\brief absolute path as resolved
    fs::path lockfile_;

    std::optional<fs::path> current_spot_file_;

    //!\brief our spot in line
    size_t spot_index_ = 0;

    //!\brief a UUID to verify that process data == file level contents
    std::string guid_;

    //!\brief our own or someone else's?
    bool owned_ = false;

    //!\brief is the spot expired
    size_t timestamp_ = 0;
  };

  //!\brief list all lockfiles given a lockfile path "waiting in line" and clear expired ones
  inline std::map<fs::path, goldilock_spot> list_lockfile_spots(const fs::path& lockfile_path) {
    std::map<fs::path, goldilock_spot> result;
    fs::path parent_path = fs::weakly_canonical(fs::path(lockfile_path)).parent_path();     

    for(auto & directory_entry : boost::filesystem::directory_iterator(parent_path)) {

      if(!directory_entry.is_regular_file()) {
        continue;
      }

      auto locker_in_line = extract_lockfile_spot_index(lockfile_path, directory_entry.path());

      // this means it is a potentially valid lock file
      if(locker_in_line.has_value()) {

        bool delete_spot = false;

        try {
          auto spot = goldilock_spot::read_from(directory_entry.path(), lockfile_path);
          delete_spot = spot.is_expired();

          if(!delete_spot) {
            result.insert({ directory_entry.path(), spot });
          }
        }
        catch(...) {
          std::cerr << "Warning - deleting broken lock spot:" << directory_entry.path() << std::endl;
          delete_spot = true;
        }

        if(delete_spot) {
          boost::system::error_code fsec;
          fs::remove(directory_entry.path(), fsec); // fail silently... 
        }
      }
    }

    return result;
  }
}