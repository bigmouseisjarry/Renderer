#pragma once

#include <string>
#include <cstdint>
#include <sstream>
#include <iomanip>

/**
    * @brief Format data to a hex string.
    *
    * @param data the data to be formatted.
    * @param len the length of the data.
    * @param upper whether to use upper case or lower case for hex characters, default is upper case.
    * @param separator the separator between each pair of hex characters, default is space.
    * @return std::string the formatted string.
    */
std::string ToHex(const uint8_t *data, size_t len, bool upper = true, char separator = ' ');
/**
    * @brief Format a uint8_t value to a hex string.
    *
    * @param value the value to be formatted.
    * @param upper whether to use upper case or lower case for hex characters, default is upper case.
    * @param prefix the prefix of the formatted string, default is empty.
    * @return std::string the formatted string.
    */
std::string ToHex(uint8_t value, bool upper = true, const std::string &prefix = "");
/**
    * @brief Format a uint16_t value to a hex string.
    *
    * @param value the value to be formatted.
    * @param upper whether to use upper case or lower case for hex characters, default is upper case.
    * @param prefix the prefix of the formatted string, default is empty.
    * @return std::string the formatted string.
    */
std::string ToHex(uint16_t value, bool upper = true, const std::string &prefix = "");
/**
    * @brief Format a uint32_t value to a hex string.
    *
    * @param value the value to be formatted.
    * @param upper whether to use upper case or lower case for hex characters, default is upper case.
    * @param prefix the prefix of the formatted string, default is empty.
    * @return std::string the formatted string.
    */
std::string ToHex(uint32_t value, bool upper = true, const std::string &prefix = "");
/**
    * @brief Format a uint64_t value to a hex string.
    *
    * @param value the value to be formatted.
    * @param upper whether to use upper case or lower case for hex characters, default is upper case.
    * @param prefix the prefix of the formatted string, default is empty.
    * @return std::string the formatted string.
    */
std::string ToHex(uint64_t value, bool upper = true, const std::string &prefix = "");