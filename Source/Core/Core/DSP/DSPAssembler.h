// Copyright 2009 Dolphin Emulator Project
// Copyright 2005 Duddie
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"

#include "Core/DSP/DSPDisassembler.h"
#include "Core/DSP/DSPTables.h"
#include "Core/DSP/LabelMap.h"

namespace DSP
{
enum class AssemblerError
{
  OK,
  Unknown,
  UnknownOpcode,
  NotEnoughParameters,
  TooManyParameters,
  WrongParameter,
  ExpectedParamStr,
  ExpectedParamVal,
  ExpectedParamReg,
  ExpectedParamMem,
  ExpectedParamImm,
  IncorrectBinary,
  IncorrectHex,
  IncorrectDecimal,
  LabelAlreadyExists,
  UnknownLabel,
  NoMatchingBrackets,
  CantExtendOpcode,
  ExtensionParamsOnNonExtendableOpcode,
  WrongParameterExpectedAccumulator,
  WrongParameterExpectedMidAccumulator,
  InvalidRegister,
  NumberOutOfRange,
  PCOutOfRange,
};

// Unless you want labels to carry over between files, you probably
// want to create a new DSPAssembler for every file you assemble.
class DSPAssembler
{
public:
  explicit DSPAssembler(const AssemblerSettings& settings);
  ~DSPAssembler();

  // line_numbers is optional (and not yet implemented). It'll receieve a list of ints,
  // one for each word of code, indicating the source assembler code line number it came from.

  // If returns false, call GetErrorString to get some text to present to the user.
  bool Assemble(const std::string& text, std::vector<u16>& code,
    std::vector<int>* line_numbers = nullptr);

  std::string GetErrorString() const { return last_error_str; }
  AssemblerError GetError() const { return last_error; }
private:
  struct param_t
  {
    u32 val;
    partype_t type;
    char* str;
  };

  enum segment_t
  {
    SEGMENT_CODE = 0,
    SEGMENT_DATA,
    SEGMENT_OVERLAY,
    SEGMENT_MAX
  };

  enum class OpcodeType
  {
    Primary,
    Extension
  };

  // Utility functions
  s32 ParseValue(const char* str);
  u32 ParseExpression(const char* ptr);

  u32 GetParams(char* parstr, param_t* par);

  void InitPass(int pass);
  bool AssemblePass(const std::string& text, int pass);

  void ShowError(AssemblerError err_code, const char* extra_info = nullptr);

  char* FindBrackets(char* src, char* dst);
  const DSPOPCTemplate* FindOpcode(std::string name, size_t par_count, OpcodeType type);
  bool VerifyParams(const DSPOPCTemplate* opc, param_t* par, size_t count, OpcodeType type);
  void BuildCode(const DSPOPCTemplate* opc, param_t* par, u32 par_count, u16* outbuf);

  std::vector<u16> m_output_buffer;

  std::string include_dir;
  std::string cur_line;

  u32 m_cur_addr;
  int m_totalSize;
  u8 m_cur_pass;

  LabelMap labels;

  u32 code_line;
  bool failed;
  std::string last_error_str;
  AssemblerError last_error;

  typedef std::map<std::string, std::string> AliasMap;
  AliasMap aliases;

  segment_t cur_segment;
  u32 segment_addr[SEGMENT_MAX];
  int m_current_param;
  const AssemblerSettings settings_;
};
}  // namespace DSP
