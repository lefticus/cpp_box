#include <array>
#include <cassert>
#include <iostream>

// todo: make asserts into aborts if necessary?

namespace arm_thing::elf {

// todo, move this into a shared utility location
template<std::size_t Bytes, typename Data>
[[nodiscard]] constexpr auto read_loc(const Data &data, const std::size_t loc, [[maybe_unused]] const bool little_endian) noexcept
{
  // todo : assert size is within bounds?
  if constexpr (Bytes == 1) {
    return static_cast<std::uint8_t>(data[loc]);
  } else if constexpr (Bytes == 2) {
    const std::uint16_t byte0 = data[loc];
    const std::uint16_t byte1 = data[loc + 1];
    if (little_endian) {
      return static_cast<std::uint16_t>(byte0 | (byte1 << 8));
    } else {
      return static_cast<std::uint16_t>((byte0 << 8) | byte1);
    }
  } else if constexpr (Bytes == 4) {
    const std::uint32_t byte0 = data[loc];
    const std::uint32_t byte1 = data[loc + 1];
    const std::uint32_t byte2 = data[loc + 2];
    const std::uint32_t byte3 = data[loc + 3];
    if (little_endian) {
      return byte0 | (byte1 << 8) | (byte2 << 16) | (byte3 << 24);
    } else {
      return (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3;
    }
  } else if constexpr (Bytes == 8) {
    const std::uint64_t byte0 = data[loc];
    const std::uint64_t byte1 = data[loc + 1];
    const std::uint64_t byte2 = data[loc + 2];
    const std::uint64_t byte3 = data[loc + 3];
    const std::uint64_t byte4 = data[loc + 4];
    const std::uint64_t byte5 = data[loc + 5];
    const std::uint64_t byte6 = data[loc + 6];
    const std::uint64_t byte7 = data[loc + 7];
    if (little_endian) {
      return byte0 | (byte1 << 8) | (byte2 << 16) | (byte3 << 24) | (byte4 << 32) | (byte5 << 40) | (byte6 << 48) | (byte7 << 56);
    } else {
      return (byte0 << 56) | (byte1 << 48) | (byte2 << 40) | (byte3 << 32) | (byte4 << 24) | (byte5 << 16) | (byte6 << 8) | byte7;
    }
  } else {
    static_assert(Bytes == 1 || Bytes == 2 || Bytes == 4 || Bytes == 8);
  }
}

template<typename Getter>[[nodiscard]] constexpr auto make_iterator(Getter t_getter, const std::size_t t_max) noexcept
{
  struct Const_Iterator
  {
    std::size_t idx{};
    Getter getter;
    [[nodiscard]] constexpr auto operator*() const noexcept { return getter(idx); }
    constexpr Const_Iterator &operator++() noexcept
    {
      ++idx;
      return *this;
    }
    [[nodiscard]] constexpr bool operator!=(const Const_Iterator &other) const noexcept { return idx != other.idx; }
  };

  struct Iterable
  {
    [[nodiscard]] constexpr Const_Iterator begin() const noexcept { return Const_Iterator{ 0, getter }; }
    [[nodiscard]] constexpr Const_Iterator end() const noexcept { return Const_Iterator{ max, getter }; }

    std::size_t max{};
    Getter getter;
  };

  return Iterable{ t_max, t_getter };
}


// https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-79797.html#chapter6-tbl-23
struct Symbol_Table_Entry
{
  enum class Fields { st_name, st_value, st_size, st_info, st_other, st_shndx };

  enum class Binding {
    STB_LOCAL  = 0,
    STB_GLOBAL = 1,
    STB_WEAK   = 2,
    STB_LOOS   = 10,
    STB_HIOS   = 12,
    STB_LOPROC = 13,
    STB_HIPROC = 15,
  };

  enum class Type {
    STT_NOTYPE         = 0,
    STT_OBJECT         = 1,
    STT_FUNC           = 2,
    STT_SECTION        = 3,
    STT_FILE           = 4,
    STT_COMMON         = 5,
    STT_TLS            = 6,
    STT_LOOS           = 10,
    STT_HIOS           = 12,
    STT_LOPROC         = 13,
    STT_SPARC_REGISTER = 13,
    STT_HIPROC         = 15
  };


  enum class Visibility {
    STV_DEFAULT   = 0,
    STV_INTERNAL  = 1,
    STV_HIDDEN    = 2,
    STV_PROTECTED = 3,
    STV_EXPORTED  = 4,
    STV_SINGLETON = 5,
    STV_ELIMINATE = 6
  };

  bool bits_32       = true;
  bool little_endian = true;
  std::basic_string_view<std::uint8_t> full_data;
  std::basic_string_view<std::uint8_t> data;

  [[nodiscard]] constexpr static auto entry_size(const bool t_bits_32) noexcept -> std::size_t
  {
    if (t_bits_32) {
      return 16;
    } else {
      return 24;
    }
  }

  constexpr Symbol_Table_Entry(const bool t_bits_32,
                               const bool t_little_endian,
                               const std::basic_string_view<std::uint8_t> t_full_data,
                               const std::basic_string_view<std::uint8_t> t_data) noexcept
    : bits_32{ t_bits_32 }, little_endian{ t_little_endian }, full_data{ t_full_data }, data{ t_data }
  {
  }

  [[nodiscard]] constexpr auto offset(const Fields field) const noexcept -> std::size_t
  {
    switch (field) {
    case Fields::st_name: return 0;
    case Fields::st_value: return bits_32 ? 4 : 8;
    case Fields::st_size: return bits_32 ? 8 : 16;
    case Fields::st_info: return bits_32 ? 12 : 4;
    case Fields::st_other: return bits_32 ? 13 : 5;
    case Fields::st_shndx: return bits_32 ? 14 : 6;
    default: assert(false);
    }
  }

  [[nodiscard]] constexpr auto size(const Fields field) const noexcept
  {
    switch (field) {
    case Fields::st_name: return 4;
    case Fields::st_value: return bits_32 ? 4 : 8;
    case Fields::st_size: return bits_32 ? 4 : 8;
    case Fields::st_info: return 1;
    case Fields::st_other: return 1;
    case Fields::st_shndx: return 2;
    default: assert(false);
    }
  }

  [[nodiscard]] constexpr auto name_offset() const noexcept { return read(Fields::st_name); }

  [[nodiscard]] constexpr auto section_header_table_index() const noexcept { return read(Fields::st_shndx); }

  [[nodiscard]] constexpr auto name(const std::string_view string_table) const noexcept -> std::string_view
  {
    const auto name_loc   = name_offset();
    const auto name_start = string_table.substr(name_loc);
    return name_start.substr(0, name_start.find('\0'));
  }

  [[nodiscard]] constexpr auto value() const noexcept { return read(Fields::st_value); }

  [[nodiscard]] constexpr auto size() const noexcept { return read(Fields::st_size); }


  [[nodiscard]] constexpr auto read(const Fields field) const noexcept -> std::uint64_t
  {
    switch (size(field)) {
    case 1: return read_loc<1>(data, offset(field), true);
    case 2: return read_loc<2>(data, offset(field), little_endian);
    case 4: return read_loc<4>(data, offset(field), little_endian);
    case 8: return read_loc<8>(data, offset(field), little_endian);
    default: assert(false);
    }
  }
};

struct Section_Header
{
  enum class Fields { sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize };

  enum class Types {
    SHT_NULL          = 0x00,        // Section header table entry unused
    SHT_PROGBITS      = 0x01,        // Program data
    SHT_SYMTAB        = 0x02,        // Symbol table
    SHT_STRTAB        = 0x03,        // String table
    SHT_RELA          = 0x04,        // Relocation entries with addends
    SHT_HASH          = 0x05,        // Symbol hash table
    SHT_DYNAMIC       = 0x06,        // Dynamic linking information
    SHT_NOTE          = 0x07,        // Notes
    SHT_NOBITS        = 0x08,        // Program space with no data (bss)
    SHT_REL           = 0x09,        // Relocation entries, no addends
    SHT_SHLIB         = 0x0A,        // Reserved
    SHT_DYNSYM        = 0x0B,        // Dynamic linker symbol table
    SHT_INIT_ARRAY    = 0x0E,        // Array of constructors
    SHT_FINI_ARRAY    = 0x0F,        // Array of destructors
    SHT_PREINIT_ARRAY = 0x10,        // Array of pre-constructors
    SHT_GROUP         = 0x11,        // Section group
    SHT_SYMTAB_SHNDX  = 0x12,        // Extended section indices
    SHT_NUM           = 0x13,        // Number of defined types.
    SHT_LOOS          = 0x60000000,  // Start OS-specific.
    Unknown
  };

  enum class Flags : std::uint32_t {
    SHF_WRITE            = 0x1,         // Writable
    SHF_ALLOC            = 0x2,         // Occupies memory during execution
    SHF_EXECINSTR        = 0x4,         // Executable
    SHF_MERGE            = 0x10,        // Might be merged
    SHF_STRINGS          = 0x20,        // Contains nul-terminated strings
    SHF_INFO_LINK        = 0x40,        // 'sh_info' contains SHT index
    SHF_LINK_ORDER       = 0x80,        // Preserve order after combining
    SHF_OS_NONCONFORMING = 0x100,       // Non-standard OS specific handling required
    SHF_GROUP            = 0x200,       // Section is member of a group
    SHF_TLS              = 0x400,       // Section hold thread-local data
    SHF_MASKOS           = 0x0ff00000,  // OS-specific
    SHF_MASKPROC         = 0xf0000000,  // Processor-specific
    SHF_ORDERED          = 0x4000000,   // Special ordering requirement (Solaris)
    SHF_EXCLUDE          = 0x8000000    // Section is excluded unless referenced or allocated (Solaris)
  };

  bool bits_32       = true;
  bool little_endian = true;
  std::basic_string_view<std::uint8_t> full_data;
  std::basic_string_view<std::uint8_t> data;

  constexpr Section_Header(const bool t_bits_32,
                           const bool t_little_endian,
                           const std::basic_string_view<std::uint8_t> t_full_data,
                           const std::basic_string_view<std::uint8_t> t_data)
    : bits_32{ t_bits_32 }, little_endian{ t_little_endian }, full_data{ t_full_data }, data{ t_data }
  {
  }

  [[nodiscard]] constexpr auto offset(const Fields field) const noexcept -> std::size_t
  {
    switch (field) {
    case Fields::sh_name: return 0x00;
    case Fields::sh_type: return 0x04;
    case Fields::sh_flags: return 0x08;
    case Fields::sh_addr: return bits_32 ? 0x0C : 0x10;
    case Fields::sh_offset: return bits_32 ? 0x10 : 0x18;
    case Fields::sh_size: return bits_32 ? 0x14 : 0x20;
    case Fields::sh_link: return bits_32 ? 0x18 : 0x28;
    case Fields::sh_info: return bits_32 ? 0x1C : 0x2C;
    case Fields::sh_addralign: return bits_32 ? 0x20 : 0x30;
    case Fields::sh_entsize: return bits_32 ? 0x24 : 0x38;
    default: assert(false);
    }
  }

  [[nodiscard]] constexpr auto size(const Fields field) const noexcept
  {
    switch (field) {
    case Fields::sh_name: return 4;
    case Fields::sh_type: return 4;
    case Fields::sh_flags: return bits_32 ? 4 : 8;
    case Fields::sh_addr: return bits_32 ? 4 : 8;
    case Fields::sh_offset: return bits_32 ? 4 : 8;
    case Fields::sh_size: return bits_32 ? 4 : 8;
    case Fields::sh_link: return 4;
    case Fields::sh_info: return 4;
    case Fields::sh_addralign: return bits_32 ? 4 : 8;
    case Fields::sh_entsize: return bits_32 ? 4 : 8;
    default: assert(false);
    }
  }

  [[nodiscard]] constexpr auto type() const noexcept -> Types
  {
    switch (const auto val = static_cast<Types>(read(Fields::sh_type)); val) {
    case Types::SHT_NULL:
    case Types::SHT_PROGBITS:
    case Types::SHT_SYMTAB:
    case Types::SHT_STRTAB:
    case Types::SHT_RELA:
    case Types::SHT_HASH:
    case Types::SHT_DYNAMIC:
    case Types::SHT_NOTE:
    case Types::SHT_NOBITS:
    case Types::SHT_REL:
    case Types::SHT_SHLIB:
    case Types::SHT_DYNSYM:
    case Types::SHT_INIT_ARRAY:
    case Types::SHT_FINI_ARRAY:
    case Types::SHT_PREINIT_ARRAY:
    case Types::SHT_GROUP:
    case Types::SHT_SYMTAB_SHNDX:
    case Types::SHT_NUM:
    case Types::SHT_LOOS:
    case Types::Unknown: return val;
    }

    return Types::Unknown;
  }

  [[nodiscard]] constexpr auto name_offset() const noexcept { return read(Fields::sh_name); }

  [[nodiscard]] constexpr auto offset() const noexcept { return read(Fields::sh_offset); }

  [[nodiscard]] constexpr auto name(const std::string_view string_table) const noexcept -> std::string_view
  {
    const auto name_loc   = name_offset();
    const auto name_start = string_table.substr(name_loc);
    return name_start.substr(0, name_start.find('\0'));
  }

  [[nodiscard]] constexpr auto size() const noexcept { return read(Fields::sh_size); }

  [[nodiscard]] constexpr auto read(const Fields field) const noexcept -> std::uint64_t
  {
    switch (size(field)) {
    case 1: return read_loc<1>(data, offset(field), true);
    case 2: return read_loc<2>(data, offset(field), little_endian);
    case 4: return read_loc<4>(data, offset(field), little_endian);
    case 8: return read_loc<8>(data, offset(field), little_endian);
    default: assert(false);
    }
  }

  [[nodiscard]] constexpr auto section_data() const noexcept -> std::basic_string_view<std::uint8_t> { return full_data.substr(offset(), size()); }

  [[nodiscard]] constexpr auto symbol_table_num_entries() const noexcept -> std::size_t
  {
    if (type() == Types::SHT_SYMTAB) {
      return size() / Symbol_Table_Entry::entry_size(bits_32);
    } else {
      return 0;
    }
  }

  [[nodiscard]] constexpr auto symbol_table_entry(const std::size_t index) const noexcept -> Symbol_Table_Entry
  {
    const auto size = Symbol_Table_Entry::entry_size(bits_32);
    return Symbol_Table_Entry{ bits_32, little_endian, full_data, section_data().substr(size * index, size) };
  }

  [[nodiscard]] constexpr auto symbol_table_entries() const noexcept
  {
    return make_iterator([this](const std::size_t idx) { return symbol_table_entry(idx); }, symbol_table_num_entries());
  }



};

struct File_Header
{
  enum class Fields {
    ei_mag,
    ei_mag_0,
    ei_mag_1,
    ei_mag_2,
    ei_mag_3,
    ei_class,
    ei_data,
    ei_version,
    ei_osabi,
    ei_abiversion,
    ei_pad,
    e_type,
    e_machine,
    e_version,
    e_entry,
    e_phoff,
    e_shoff,
    e_flags,
    e_ehsize,
    e_phentsize,
    e_phnum,
    e_shentsize,
    e_shnum,
    e_shstrndx
  };


  [[nodiscard]] constexpr auto offset(const Fields field) const noexcept -> std::size_t
  {
    switch (field) {
    case Fields::ei_mag: return 0x00;
    case Fields::ei_mag_0: return 0x00;
    case Fields::ei_mag_1: return 0x01;
    case Fields::ei_mag_2: return 0x02;
    case Fields::ei_mag_3: return 0x03;
    case Fields::ei_class: return 0x04;
    case Fields::ei_data: return 0x05;
    case Fields::ei_version: return 0x06;
    case Fields::ei_osabi: return 0x07;
    case Fields::ei_abiversion: return 0x08;
    case Fields::ei_pad: return 0x09;
    case Fields::e_type: return 0x10;
    case Fields::e_machine: return 0x12;
    case Fields::e_version: return 0x14;
    case Fields::e_entry: return 0x18;
    case Fields::e_phoff: return bits_32() ? 0x1C : 0x20;
    case Fields::e_shoff: return bits_32() ? 0x20 : 0x28;
    case Fields::e_flags: return bits_32() ? 0x24 : 0x30;
    case Fields::e_ehsize: return bits_32() ? 0x28 : 0x34;
    case Fields::e_phentsize: return bits_32() ? 0x2A : 0x36;
    case Fields::e_phnum: return bits_32() ? 0x2C : 0x38;
    case Fields::e_shentsize: return bits_32() ? 0x2E : 0x3A;
    case Fields::e_shnum: return bits_32() ? 0x30 : 0x3C;
    case Fields::e_shstrndx: return bits_32() ? 0x32 : 0x3E;
    default: assert(false);
    }
  }

  [[nodiscard]] constexpr auto size(const Fields field) const noexcept
  {
    switch (field) {
    case Fields::ei_mag: return 4;
    case Fields::ei_mag_0: return 1;
    case Fields::ei_mag_1: return 1;
    case Fields::ei_mag_2: return 1;
    case Fields::ei_mag_3: return 1;
    case Fields::ei_class: return 1;
    case Fields::ei_data: return 1;
    case Fields::ei_version: return 1;
    case Fields::ei_osabi: return 1;
    case Fields::ei_abiversion: return 1;
    case Fields::ei_pad: return 7;
    case Fields::e_type: return 2;
    case Fields::e_machine: return 2;
    case Fields::e_version: return 4;
    case Fields::e_entry: return bits_32() ? 4 : 8;
    case Fields::e_phoff: return bits_32() ? 4 : 8;
    case Fields::e_shoff: return bits_32() ? 4 : 8;
    case Fields::e_flags: return 4;
    case Fields::e_ehsize: return 2;
    case Fields::e_phentsize: return 2;
    case Fields::e_phnum: return 2;
    case Fields::e_shentsize: return 2;
    case Fields::e_shnum: return 2;
    case Fields::e_shstrndx: return 2;
    default: assert(false);
    }
  }


  enum class Class { bits_32 = 1, bits_64 = 2, Unknown };

  enum class Data { little_endian = 1, big_endian = 2, Unknown };

  enum class OSABI {
    System_V       = 0x00,
    HP_UX          = 0x01,
    NetBSD         = 0x02,
    Linux          = 0x03,
    GNU_Hurd       = 0x04,
    Solaris        = 0x06,
    AIX            = 0x07,
    IRIX           = 0x08,
    FreeBSD        = 0x09,
    Tru64          = 0x0A,
    Novell_Modesto = 0x0B,
    OpenBSD        = 0x0C,
    OpenVMS        = 0x0D,
    NonStop_Kernel = 0x0E,
    AROS           = 0x0F,
    Fenix_OS       = 0x10,
    CloudABI       = 0x11,
    Unknown
  };

  enum class ObjectType {
    ET_NONE   = 0x00,
    ET_REL    = 0x01,
    ET_EXEC   = 0x02,
    ET_DYN    = 0x03,
    ET_CORE   = 0x04,
    ET_LOOS   = 0xFE00,
    ET_HIOS   = 0xFEFF,
    ET_LOPROC = 0xFF00,
    ET_HIPROC = 0xFFFF,
    Unknown
  };

  enum class Machine {
    Unknown = 0x00,
    SPARC   = 0x02,
    x86     = 0x03,
    MIPS    = 0x08,
    PowerPC = 0x14,
    S390    = 0x16,
    ARM     = 0x28,
    SuperH  = 0x2A,
    IA_64   = 0x32,
    x86_64  = 0x3E,
    AArch64 = 0xB7,
    RISC_V  = 0xF3
  };


  constexpr File_Header(std::basic_string_view<std::uint8_t> t_data) : data(t_data) { assert(data.size() >= 64); }

  std::basic_string_view<std::uint8_t> data;

  // todo: move to shared code
  template<typename Itr, typename Itr2>[[nodiscard]] constexpr bool equal(Itr begin1, const Itr end1, Itr2 begin2) const noexcept
  {
    while (begin1 != end1) {
      if (*begin1 != *begin2) { return false; }
      ++begin1;
      ++begin2;
    }
    return true;
  }

  [[nodiscard]] constexpr bool is_elf_file() const noexcept
  {
    const auto magic_number = { 0x7F, 0x45, 0x4C, 0x46 };
    return equal(std::begin(magic_number), std::end(magic_number), std::begin(data));
  }

  [[nodiscard]] constexpr auto bit_class() const noexcept -> Class
  {
    switch (const auto val = static_cast<Class>(read(Fields::ei_class)); val) {
    case Class::bits_32:
    case Class::bits_64:
    case Class::Unknown: return val;
    }

    return Class::Unknown;
  }

  [[nodiscard]] constexpr auto multibyte_data() const noexcept -> Data
  {
    switch (const auto val = static_cast<Data>(read(Fields::ei_data)); val) {
    case Data::little_endian:
    case Data::big_endian:
    case Data::Unknown: return val;
    }

    return Data::Unknown;
  }

  [[nodiscard]] constexpr auto little_endian() const noexcept -> bool
  {
    switch (multibyte_data()) {
    case Data::little_endian: return true;
    case Data::big_endian: return false;
    default: assert(false);
    }
  }

  [[nodiscard]] constexpr auto bits_32() const noexcept -> bool
  {
    switch (bit_class()) {
    case Class::bits_32: return true;
    case Class::bits_64: return false;
    default: assert(false);
    }
  }

  [[nodiscard]] constexpr auto elf_version() const noexcept { return read(Fields::ei_version); }

  [[nodiscard]] constexpr auto os_abi() const noexcept -> OSABI
  {
    switch (const auto val = static_cast<OSABI>(read(Fields::ei_osabi)); val) {
    case OSABI::System_V:
    case OSABI::HP_UX:
    case OSABI::NetBSD:
    case OSABI::Linux:
    case OSABI::GNU_Hurd:
    case OSABI::Solaris:
    case OSABI::AIX:
    case OSABI::IRIX:
    case OSABI::FreeBSD:
    case OSABI::Tru64:
    case OSABI::Novell_Modesto:
    case OSABI::OpenBSD:
    case OSABI::OpenVMS:
    case OSABI::NonStop_Kernel:
    case OSABI::AROS:
    case OSABI::Fenix_OS:
    case OSABI::CloudABI:
    case OSABI::Unknown: return val;
    }

    return OSABI::Unknown;
  }

  [[nodiscard]] constexpr auto abi_version() const noexcept { return read(Fields::ei_abiversion); }

  [[nodiscard]] constexpr auto type() const noexcept -> ObjectType
  {
    switch (const auto val = static_cast<ObjectType>(read(Fields::e_type)); val) {
    case ObjectType::ET_NONE:
    case ObjectType::ET_REL:
    case ObjectType::ET_EXEC:
    case ObjectType::ET_DYN:
    case ObjectType::ET_CORE:
    case ObjectType::ET_LOOS:
    case ObjectType::ET_HIOS:
    case ObjectType::ET_LOPROC:
    case ObjectType::ET_HIPROC:
    case ObjectType::Unknown: return val;
    }

    return ObjectType::Unknown;
  }


  [[nodiscard]] constexpr auto machine() const noexcept -> Machine
  {
    switch (const auto val = static_cast<Machine>(read(Fields::e_machine)); val) {
    case Machine::Unknown:
    case Machine::SPARC:
    case Machine::x86:
    case Machine::MIPS:
    case Machine::PowerPC:
    case Machine::S390:
    case Machine::ARM:
    case Machine::SuperH:
    case Machine::IA_64:
    case Machine::x86_64:
    case Machine::AArch64:
    case Machine::RISC_V: return val;
    }

    return Machine::Unknown;
  }

  [[nodiscard]] constexpr auto version() const noexcept { return read(Fields::e_version); }

  [[nodiscard]] constexpr auto entry() const noexcept { return read(Fields::e_entry); }

  [[nodiscard]] constexpr auto program_header_offset() const noexcept { return read(Fields::e_phoff); }

  [[nodiscard]] constexpr auto section_header_offset() const noexcept { return read(Fields::e_shoff); }

  [[nodiscard]] constexpr auto flags() const noexcept { return read(Fields::e_flags); }
  [[nodiscard]] constexpr auto size() const noexcept { return read(Fields::e_ehsize); }
  [[nodiscard]] constexpr auto program_header_size() const noexcept { return read(Fields::e_phentsize); }
  [[nodiscard]] constexpr auto program_header_num_entries() const noexcept { return read(Fields::e_phnum); }
  [[nodiscard]] constexpr auto section_header_size() const noexcept { return read(Fields::e_shentsize); }
  [[nodiscard]] constexpr auto section_header_num_entries() const noexcept { return read(Fields::e_shnum); }
  [[nodiscard]] constexpr auto section_header_string_table_index() const noexcept { return read(Fields::e_shstrndx); }

  [[nodiscard]] constexpr auto section_header(const std::size_t entry) const noexcept -> Section_Header
  {
    assert(entry < section_header_num_entries());
    return { bits_32(), little_endian(), data, data.substr(section_header_offset() + section_header_size() * entry) };
  }

  [[nodiscard]] constexpr auto sh_string_table() const noexcept -> std::string_view
  {
    const auto string_header{ section_header(section_header_string_table_index()) };
    const auto string_data{ string_header.section_data() };
    return std::string_view{ reinterpret_cast<const char *>(string_data.data()), string_data.size() };
  }



  [[nodiscard]] constexpr auto section_headers() const noexcept
  {
    return make_iterator([this](const std::size_t idx) { return section_header(idx); }, section_header_num_entries());
  }

  [[nodiscard]] constexpr auto string_table() const noexcept -> std::string_view
  {
    const auto sh_table = sh_string_table();

    for (const auto &section_header : section_headers()) {
      if (section_header.name(sh_table) == ".strtab") {
        const auto string_data{ section_header.section_data() };
        return std::string_view{ reinterpret_cast<const char *>(string_data.data()), string_data.size() };
      }
    }

  }


  [[nodiscard]] constexpr auto read(const Fields field) const noexcept -> std::uint64_t
  {
    switch (size(field)) {
    case 1: return read_loc<1>(data, offset(field), true);
    case 2: return read_loc<2>(data, offset(field), little_endian());
    case 4: return read_loc<4>(data, offset(field), little_endian());
    case 8: return read_loc<8>(data, offset(field), little_endian());
    default: assert(false);
    }
  }
};
}  // namespace arm_thing::ELF
