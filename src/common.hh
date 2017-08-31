/** 
Copyright (C) 2016 Gregory Vaumourin

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**/

#ifndef COMMON_HPP
#define COMMON_HPP

#include <vector>
#include <stdint.h>
#include <string>

#ifdef TEST
	#define DPRINTF(...) printf(__VA_ARGS__);
#else
	#define DPRINTF(...) 
#endif

#define CONFIG_FILE "config.ini"
#define OUTPUT_FILE "results.out"
#define LOG_FILE "log.out"

#define PREDICTOR_TIME_FRAME 1E7

#define ONE_MB 1048576
#define TWO_MB 2*ONE_MB
#define FOUR_MB 2*TWO_MB


#define BLOCK_SIZE 64

/**
	Hold utilitary functions 
*/

std::vector<std::string> split(std::string s, char delimiter);
uint64_t bitRemove(uint64_t address , unsigned int small, unsigned int big);
uint64_t hexToInt(std::string adresse_hex);
uint64_t hexToInt1(const char* adresse_hex);

bool readInputArgs(int argc , char* argv[] , int& sizeCache , int& assoc , int& blocksize, std::string& filename, std::string& policy);
bool isPow2(int x);
std::string convert_hex(int n);
const char * StripPath(const char * path);


extern uint64_t cpt_time;
extern int start_debug;

extern const char* memCmd_str[];
extern const char* allocDecision_str[];
extern const char* directory_state_str[];


#endif 
