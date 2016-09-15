#ifndef SQL_TYPE_H_INCLUDED
#define SQL_TYPE_H_INCLUDED
/*
   Copyright (c) 2015  MariaDB Foundation.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; version 2 of the License.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif


#include "mysqld.h"

class Field;
class Item;
class Type_std_attributes;
class Sort_param;
struct TABLE;
struct SORT_FIELD_ATTR;

class Value_converter
{
protected:

  // Parameters for warning and note generation
  class Warn_filter
  {
    bool m_want_warning_edom;
    bool m_want_note_truncated_spaces;
  public:
    Warn_filter(bool want_warning_edom, bool want_note_truncated_spaces) :
     m_want_warning_edom(want_warning_edom),
     m_want_note_truncated_spaces(want_note_truncated_spaces)
    { }
    Warn_filter(const THD *thd);
    bool want_warning_edom() const
    { return m_want_warning_edom; }
    bool want_note_truncated_spaces() const
    { return m_want_note_truncated_spaces; }
  };
  class Warn_filter_all: public Warn_filter
  {
  public:
    Warn_filter_all() :Warn_filter(true, true) { }
  };

  class Converter_double_to_longlong
  {
  protected:
    bool m_error;
    longlong m_result;
  public:
    Converter_double_to_longlong(double nr, bool unsigned_flag);
    longlong result() const { return m_result; }
    bool error() const { return m_error; }
    void push_warning(THD *thd, double nr, bool unsigned_flag);
  };
  class Converter_double_to_longlong_with_warn:
    public Converter_double_to_longlong
  {
  public:
    Converter_double_to_longlong_with_warn(THD *thd, double nr,
                                           bool unsigned_flag)
      :Converter_double_to_longlong(nr, unsigned_flag)
    {
      if (m_error)
        push_warning(thd, nr, unsigned_flag);
    }
    Converter_double_to_longlong_with_warn(double nr, bool unsigned_flag)
      :Converter_double_to_longlong(nr, unsigned_flag)
    {
      if (m_error)
        push_warning(current_thd, nr, unsigned_flag);
    }
  };

  // String-to-number converters
  class Converter_string_to_number
  {
  protected:
    char *m_end_of_num; // Where the low-level conversion routine stopped
    int m_error;        // The error code returned by the low-level routine
    bool m_edom;        // If EDOM-alike error happened during conversion
    /**
      Check string-to-number conversion and produce a warning if
      - could not convert any digits (EDOM-alike error)
      - found garbage at the end of the string
      - found extra spaces at the end (a note)
      See also Field_num::check_edom_and_truncation() for a similar function.

      @param thd         - the thread that will be used to generate warnings.
                           Can be NULL (which means current_thd will be used
                           if a warning is really necessary).
      @param type        - name of the data type
                           (e.g. "INTEGER", "DECIMAL", "DOUBLE")
      @param cs          - character set of the original string
      @param str         - the original string
      @param end         - the end of the string
      @param allow_notes - tells if trailing space notes should be displayed
                           or suppressed.

      Unlike Field_num::check_edom_and_truncation(), this function does not
      distinguish between EDOM and truncation and reports the same warning for
      both cases. Perhaps we should eventually print different warnings,
      to make the explicit CAST work closer to the implicit cast in
      Field_xxx::store().
    */
    void check_edom_and_truncation(THD *thd, Warn_filter filter,
                                   const char *type,
                                   CHARSET_INFO *cs,
                                   const char *str,
                                   size_t length) const;
  public:
    int error() const { return m_error; }
  };

  class Converter_strntod: public Converter_string_to_number
  {
    double m_result;
  public:
    Converter_strntod(CHARSET_INFO *cs, const char *str, size_t length)
    {
      m_result= my_strntod(cs, (char *) str, length, &m_end_of_num, &m_error);
      // strntod() does not set an error if the input string was empty
      m_edom= m_error !=0 || str == m_end_of_num;
    }
    double result() const { return m_result; }
  };

  class Converter_string_to_longlong: public Converter_string_to_number
  {
  protected:
    longlong m_result;
  public:
    longlong result() const { return m_result; }
  };

  class Converter_strntoll: public Converter_string_to_longlong
  {
  public:
    Converter_strntoll(CHARSET_INFO *cs, const char *str, size_t length)
    {
      m_result= my_strntoll(cs, str, length, 10, &m_end_of_num, &m_error);
      /*
         All non-zero errors means EDOM error.
         strntoll() does not set an error if the input string was empty.
         Check it here.
         Notice the different with the same condition in Converter_strntoll10.
      */
      m_edom= m_error != 0 || str == m_end_of_num;
    }
  };

  class Converter_strtoll10: public Converter_string_to_longlong
  {
  public:
    Converter_strtoll10(CHARSET_INFO *cs, const char *str, size_t length)
    {
      m_end_of_num= (char *) str + length;
      m_result= (*(cs->cset->strtoll10))(cs, str, &m_end_of_num, &m_error);
      /*
        Negative error means "good negative number".
        Only a positive m_error value means a real error.
        strtoll10() sets error to MY_ERRNO_EDOM in case of an empty string,
        so we don't have to additionally catch empty strings here.
      */
      m_edom= m_error > 0;
    }
  };

  class Converter_str2my_decimal: public Converter_string_to_number
  {
  public:
    Converter_str2my_decimal(uint mask,
                             CHARSET_INFO *cs, const char *str, size_t length,
                             my_decimal *buf)
    {
      m_error= str2my_decimal(mask, str, length, cs,
                              buf, (const char **) &m_end_of_num);
      // E_DEC_TRUNCATED means a very minor truncation: '1e-100' -> 0
      m_edom= m_error && m_error != E_DEC_TRUNCATED;
    }
  };


  // String-to-number converters with automatic warning generation
  class Converter_strntod_with_warn: public Converter_strntod
  {
  public:
    Converter_strntod_with_warn(THD *thd, Warn_filter filter,
                                CHARSET_INFO *cs,
                                const char *str, size_t length)
      :Converter_strntod(cs, str, length)
    {
      check_edom_and_truncation(thd, filter, "DOUBLE", cs, str, length);
    }
  };

  class Converter_strntoll_with_warn: public Converter_strntoll
  {
  public:
    Converter_strntoll_with_warn(THD *thd, Warn_filter filter,
                                 CHARSET_INFO *cs,
                                 const char *str, size_t length)
      :Converter_strntoll(cs, str, length)
    {
      check_edom_and_truncation(thd, filter, "INTEGER", cs, str, length);
    }
  };

  class Converter_strtoll10_with_warn: public Converter_strtoll10
  {
  public:
    Converter_strtoll10_with_warn(THD *thd, Warn_filter filter,
                                 CHARSET_INFO *cs,
                                 const char *str, size_t length)
      :Converter_strtoll10(cs, str, length)
    {
      check_edom_and_truncation(thd, filter, "INTEGER", cs, str, length);
    }
  };

  class Converter_str2my_decimal_with_warn: public Converter_str2my_decimal
  {
  public:
    Converter_str2my_decimal_with_warn(THD *thd, Warn_filter filter,
                                       uint mask, CHARSET_INFO *cs,
                                       const char *str, size_t length,
                                       my_decimal *buf)
     :Converter_str2my_decimal(mask, cs, str, length, buf)
    {
      check_edom_and_truncation(thd, filter, "DECIMAL", cs, str, length);
    }
  };


  // String-to-number convertion methods for the old code compatibility
  longlong longlong_from_string_with_check(CHARSET_INFO *cs, const char *cptr,
                                           const char *end) const
  {
    /*
      TODO: Give error if we wanted a signed integer and we got an unsigned
      one

      Notice, longlong_from_string_with_check() honors thd->no_error, because
      it's used to handle queries like this:
        SELECT COUNT(@@basedir);
      and is called when Item_func_get_system_var::update_null_value()
      suppresses warnings and then calls val_int().
      The other methods {double|decimal}_from_string_with_check() ignore
      thd->no_errors, because they are not used for update_null_value()
      and they always allow all kind of warnings.
    */
    THD *thd= current_thd;
    return Converter_strtoll10_with_warn(thd, Warn_filter(thd),
                                         cs, cptr, end - cptr).result();
  }

  double double_from_string_with_check(CHARSET_INFO *cs, const char *cptr,
                                       const char *end) const
  {
    return Converter_strntod_with_warn(NULL, Warn_filter_all(),
                                       cs, cptr, end - cptr).result();
  }
  my_decimal *decimal_from_string_with_check(my_decimal *decimal_value,
                                             CHARSET_INFO *cs,
                                             const char *cptr,
                                             const char *end)
  {
    Converter_str2my_decimal_with_warn(NULL, Warn_filter_all(),
                                       E_DEC_FATAL_ERROR & ~E_DEC_BAD_NUM,
                                       cs, cptr, end - cptr, decimal_value);
    return decimal_value;
  }

  longlong longlong_from_hex_hybrid(const char *str, uint32 length)
  {
    const char *end= str + length;
    const char *ptr= end - MY_MIN(length, sizeof(longlong));
    ulonglong value= 0;
    for ( ; ptr != end ; ptr++)
      value= (value << 8) + (ulonglong) (uchar) *ptr;
    return (longlong) value;
  }

  longlong longlong_from_string_with_check(const String *str) const
  {
    return longlong_from_string_with_check(str->charset(),
                                           str->ptr(), str->end());
  }
  double double_from_string_with_check(const String *str) const
  {
    return double_from_string_with_check(str->charset(),
                                         str->ptr(), str->end());
  }
  my_decimal *decimal_from_string_with_check(my_decimal *decimal_value,
                                             const String *str)
  {
    return decimal_from_string_with_check(decimal_value, str->charset(),
                                          str->ptr(), str->end());
  }
  // End of String-to-number conversion methods
};


class Type_handler: public Value_converter
{
protected:
  void make_sort_key_longlong(uchar *to,
                              bool maybe_null, bool null_value,
                              bool unsigned_flag,
                              longlong value) const;
public:
  static const Type_handler *string_type_handler(uint max_octet_length);
  static const Type_handler *get_handler_by_field_type(enum_field_types type);
  static const Type_handler *get_handler_by_real_type(enum_field_types type);
  virtual enum_field_types field_type() const= 0;
  virtual enum_field_types real_field_type() const { return field_type(); }
  virtual Item_result result_type() const= 0;
  virtual Item_result cmp_type() const= 0;
  virtual const Type_handler*
  type_handler_adjusted_to_max_octet_length(uint max_octet_length,
                                            CHARSET_INFO *cs) const
  { return this; }
  virtual ~Type_handler() {}
  /**
    Makes a temporary table Field to handle numeric aggregate functions,
    e.g. SUM(DISTINCT expr), AVG(DISTINCT expr), etc.
  */
  virtual Field *make_num_distinct_aggregator_field(MEM_ROOT *,
                                                    const Item *) const;
  /**
    Makes a temporary table Field to handle RBR replication type conversion.
    @param TABLE    - The conversion table the field is going to be added to.
                      It's used to access to table->in_use->mem_root,
                      to create the new field on the table memory root,
                      as well as to increment statistics in table->share
                      (e.g. table->s->blob_count).
    @param metadata - Metadata from the binary log.
    @param target   - The field in the target table on the slave.

    Note, the data types of "target" and of "this" are not necessarily
    always the same, in general case it's possible that:
            this->field_type() != target->field_type()
    and/or
            this->real_type( ) != target->real_type()

    This method decodes metadata according to this->real_type()
    and creates a new field also according to this->real_type().

    In some cases it lurks into "target", to get some extra information, e.g.:
    - unsigned_flag for numeric fields
    - charset() for string fields
    - typelib and field_length for SET and ENUM
    - geom_type and srid for GEOMETRY
    This information is not available in the binary log, so
    we assume that these fields are the same on the master and on the slave.
  */
  virtual Field *make_conversion_table_field(TABLE *TABLE,
                                             uint metadata,
                                             const Field *target) const= 0;
  virtual void make_sort_key(uchar *to, Item *item,
                             const SORT_FIELD_ATTR *sort_field,
                             Sort_param *param) const= 0;
  virtual void sortlength(THD *thd,
                          const Type_std_attributes *item,
                          SORT_FIELD_ATTR *attr) const= 0;
};


/*** Abstract classes for every XXX_RESULT */

class Type_handler_real_result: public Type_handler
{
public:
  Item_result result_type() const { return REAL_RESULT; }
  Item_result cmp_type() const { return REAL_RESULT; }
  virtual ~Type_handler_real_result() {}
  void make_sort_key(uchar *to, Item *item, const SORT_FIELD_ATTR *sort_field,
                     Sort_param *param) const;
  void sortlength(THD *thd,
                  const Type_std_attributes *item,
                  SORT_FIELD_ATTR *attr) const;
};


class Type_handler_decimal_result: public Type_handler
{
public:
  Item_result result_type() const { return DECIMAL_RESULT; }
  Item_result cmp_type() const { return DECIMAL_RESULT; }
  virtual ~Type_handler_decimal_result() {};
  Field *make_num_distinct_aggregator_field(MEM_ROOT *, const Item *) const;
  void make_sort_key(uchar *to, Item *item, const SORT_FIELD_ATTR *sort_field,
                     Sort_param *param) const;
  void sortlength(THD *thd,
                  const Type_std_attributes *item,
                  SORT_FIELD_ATTR *attr) const;
};


class Type_handler_int_result: public Type_handler
{
public:
  Item_result result_type() const { return INT_RESULT; }
  Item_result cmp_type() const { return INT_RESULT; }
  virtual ~Type_handler_int_result() {}
  Field *make_num_distinct_aggregator_field(MEM_ROOT *, const Item *) const;
  void make_sort_key(uchar *to, Item *item, const SORT_FIELD_ATTR *sort_field,
                     Sort_param *param) const;
  void sortlength(THD *thd,
                  const Type_std_attributes *item,
                  SORT_FIELD_ATTR *attr) const;
};


class Type_handler_temporal_result: public Type_handler
{
public:
  Item_result result_type() const { return STRING_RESULT; }
  Item_result cmp_type() const { return TIME_RESULT; }
  virtual ~Type_handler_temporal_result() {}
  void make_sort_key(uchar *to, Item *item,  const SORT_FIELD_ATTR *sort_field,
                     Sort_param *param) const;
  void sortlength(THD *thd,
                  const Type_std_attributes *item,
                  SORT_FIELD_ATTR *attr) const;
};


class Type_handler_string_result: public Type_handler
{
public:
  Item_result result_type() const { return STRING_RESULT; }
  Item_result cmp_type() const { return STRING_RESULT; }
  virtual ~Type_handler_string_result() {}
  const Type_handler *
  type_handler_adjusted_to_max_octet_length(uint max_octet_length,
                                            CHARSET_INFO *cs) const;
  void make_sort_key(uchar *to, Item *item, const SORT_FIELD_ATTR *sort_field,
                     Sort_param *param) const;
  void sortlength(THD *thd,
                  const Type_std_attributes *item,
                  SORT_FIELD_ATTR *attr) const;
};


/***
  Instantiable classes for every MYSQL_TYPE_XXX

  There are no Type_handler_xxx for the following types:
  - MYSQL_TYPE_VAR_STRING (old VARCHAR) - mapped to MYSQL_TYPE_VARSTRING
  - MYSQL_TYPE_ENUM                     - mapped to MYSQL_TYPE_VARSTRING
  - MYSQL_TYPE_SET:                     - mapped to MYSQL_TYPE_VARSTRING

  because the functionality that currently uses Type_handler
  (e.g. hybrid type functions) does not need to distinguish between
  these types and VARCHAR.
  For example:
    CREATE TABLE t2 AS SELECT COALESCE(enum_column) FROM t1;
  creates a VARCHAR column.

  There most likely be Type_handler_enum and Type_handler_set later,
  when the Type_handler infrastructure gets used in more pieces of the code.
*/


class Type_handler_tiny: public Type_handler_int_result
{
public:
  virtual ~Type_handler_tiny() {}
  enum_field_types field_type() const { return MYSQL_TYPE_TINY; }
  Field *make_conversion_table_field(TABLE *TABLE, uint metadata,
                                     const Field *target) const;
};


class Type_handler_short: public Type_handler_int_result
{
public:
  virtual ~Type_handler_short() {}
  enum_field_types field_type() const { return MYSQL_TYPE_SHORT; }
  Field *make_conversion_table_field(TABLE *TABLE, uint metadata,
                                     const Field *target) const;
};


class Type_handler_long: public Type_handler_int_result
{
public:
  virtual ~Type_handler_long() {}
  enum_field_types field_type() const { return MYSQL_TYPE_LONG; }
  Field *make_conversion_table_field(TABLE *TABLE, uint metadata,
                                     const Field *target) const;
};


class Type_handler_longlong: public Type_handler_int_result
{
public:
  virtual ~Type_handler_longlong() {}
  enum_field_types field_type() const { return MYSQL_TYPE_LONGLONG; }
  Field *make_conversion_table_field(TABLE *TABLE, uint metadata,
                                     const Field *target) const;
};


class Type_handler_int24: public Type_handler_int_result
{
public:
  virtual ~Type_handler_int24() {}
  enum_field_types field_type() const { return MYSQL_TYPE_INT24; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_year: public Type_handler_int_result
{
public:
  virtual ~Type_handler_year() {}
  enum_field_types field_type() const { return MYSQL_TYPE_YEAR; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_bit: public Type_handler_int_result
{
public:
  virtual ~Type_handler_bit() {}
  enum_field_types field_type() const { return MYSQL_TYPE_BIT; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_float: public Type_handler_real_result
{
public:
  virtual ~Type_handler_float() {}
  enum_field_types field_type() const { return MYSQL_TYPE_FLOAT; }
  Field *make_num_distinct_aggregator_field(MEM_ROOT *, const Item *) const;
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_double: public Type_handler_real_result
{
public:
  virtual ~Type_handler_double() {}
  enum_field_types field_type() const { return MYSQL_TYPE_DOUBLE; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_time: public Type_handler_temporal_result
{
public:
  virtual ~Type_handler_time() {}
  enum_field_types field_type() const { return MYSQL_TYPE_TIME; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_time2: public Type_handler_temporal_result
{
public:
  virtual ~Type_handler_time2() {}
  enum_field_types field_type() const { return MYSQL_TYPE_TIME; }
  enum_field_types real_field_type() const { return MYSQL_TYPE_TIME2; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_date: public Type_handler_temporal_result
{
public:
  virtual ~Type_handler_date() {}
  enum_field_types field_type() const { return MYSQL_TYPE_DATE; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_newdate: public Type_handler_temporal_result
{
public:
  virtual ~Type_handler_newdate() {}
  enum_field_types field_type() const { return MYSQL_TYPE_DATE; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_datetime: public Type_handler_temporal_result
{
public:
  virtual ~Type_handler_datetime() {}
  enum_field_types field_type() const { return MYSQL_TYPE_DATETIME; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_datetime2: public Type_handler_temporal_result
{
public:
  virtual ~Type_handler_datetime2() {}
  enum_field_types field_type() const { return MYSQL_TYPE_DATETIME; }
  enum_field_types real_field_type() const { return MYSQL_TYPE_DATETIME2; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_timestamp: public Type_handler_temporal_result
{
public:
  virtual ~Type_handler_timestamp() {}
  enum_field_types field_type() const { return MYSQL_TYPE_TIMESTAMP; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_timestamp2: public Type_handler_temporal_result
{
public:
  virtual ~Type_handler_timestamp2() {}
  enum_field_types field_type() const { return MYSQL_TYPE_TIMESTAMP; }
  enum_field_types real_field_type() const { return MYSQL_TYPE_TIMESTAMP2; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_olddecimal: public Type_handler_decimal_result
{
public:
  virtual ~Type_handler_olddecimal() {}
  enum_field_types field_type() const { return MYSQL_TYPE_DECIMAL; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_newdecimal: public Type_handler_decimal_result
{
public:
  virtual ~Type_handler_newdecimal() {}
  enum_field_types field_type() const { return MYSQL_TYPE_NEWDECIMAL; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_null: public Type_handler_string_result
{
public:
  virtual ~Type_handler_null() {}
  enum_field_types field_type() const { return MYSQL_TYPE_NULL; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_string: public Type_handler_string_result
{
public:
  virtual ~Type_handler_string() {}
  enum_field_types field_type() const { return MYSQL_TYPE_STRING; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_varchar: public Type_handler_string_result
{
public:
  virtual ~Type_handler_varchar() {}
  enum_field_types field_type() const { return MYSQL_TYPE_VARCHAR; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_tiny_blob: public Type_handler_string_result
{
public:
  virtual ~Type_handler_tiny_blob() {}
  enum_field_types field_type() const { return MYSQL_TYPE_TINY_BLOB; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_medium_blob: public Type_handler_string_result
{
public:
  virtual ~Type_handler_medium_blob() {}
  enum_field_types field_type() const { return MYSQL_TYPE_MEDIUM_BLOB; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_long_blob: public Type_handler_string_result
{
public:
  virtual ~Type_handler_long_blob() {}
  enum_field_types field_type() const { return MYSQL_TYPE_LONG_BLOB; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_blob: public Type_handler_string_result
{
public:
  virtual ~Type_handler_blob() {}
  enum_field_types field_type() const { return MYSQL_TYPE_BLOB; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


#ifdef HAVE_SPATIAL
class Type_handler_geometry: public Type_handler_string_result
{
public:
  virtual ~Type_handler_geometry() {}
  enum_field_types field_type() const { return MYSQL_TYPE_GEOMETRY; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};
#endif


class Type_handler_enum: public Type_handler_string_result
{
public:
  virtual ~Type_handler_enum() {}
  enum_field_types field_type() const { return MYSQL_TYPE_VARCHAR; }
  virtual enum_field_types real_field_type() const { return MYSQL_TYPE_ENUM; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


class Type_handler_set: public Type_handler_string_result
{
public:
  virtual ~Type_handler_set() {}
  enum_field_types field_type() const { return MYSQL_TYPE_VARCHAR; }
  virtual enum_field_types real_field_type() const { return MYSQL_TYPE_SET; }
  Field *make_conversion_table_field(TABLE *, uint metadata,
                                     const Field *target) const;
};


/**
  A handler for hybrid type functions, e.g.
  COALESCE(), IF(), IFNULL(), NULLIF(), CASE,
  numeric operators,
  UNIX_TIMESTAMP(), TIME_TO_SEC().

  Makes sure that field_type(), cmp_type() and result_type()
  are always in sync to each other for hybrid functions.
*/
class Type_handler_hybrid_field_type
{
  const Type_handler *m_type_handler;
  const Type_handler *get_handler_by_result_type(Item_result type) const;
public:
  Type_handler_hybrid_field_type();
  Type_handler_hybrid_field_type(const Type_handler *handler)
   :m_type_handler(handler)
  { }
  Type_handler_hybrid_field_type(enum_field_types type)
    :m_type_handler(Type_handler::get_handler_by_field_type(type))
  { }
  Type_handler_hybrid_field_type(const Type_handler_hybrid_field_type *other)
    :m_type_handler(other->m_type_handler)
  { }
  const Type_handler *type_handler() const { return m_type_handler; }
  enum_field_types field_type() const { return m_type_handler->field_type(); }
  enum_field_types real_field_type() const
  {
    return m_type_handler->real_field_type();
  }
  Item_result result_type() const { return m_type_handler->result_type(); }
  Item_result cmp_type() const { return m_type_handler->cmp_type(); }
  void set_handler(const Type_handler *other)
  {
    m_type_handler= other;
  }
  const Type_handler *set_handler_by_result_type(Item_result type)
  {
    return (m_type_handler= get_handler_by_result_type(type));
  }
  const Type_handler *set_handler_by_result_type(Item_result type,
                                                 uint max_octet_length,
                                                 CHARSET_INFO *cs)
  {
    m_type_handler= get_handler_by_result_type(type);
    return m_type_handler=
      m_type_handler->type_handler_adjusted_to_max_octet_length(max_octet_length,
                                                                cs);
  }
  const Type_handler *set_handler_by_field_type(enum_field_types type)
  {
    return (m_type_handler= Type_handler::get_handler_by_field_type(type));
  }
  const Type_handler *set_handler_by_real_type(enum_field_types type)
  {
    return (m_type_handler= Type_handler::get_handler_by_real_type(type));
  }
};


/**
  This class is used for Item_type_holder, which preserves real_type.
*/
class Type_handler_hybrid_real_field_type:
  public Type_handler_hybrid_field_type
{
public:
  Type_handler_hybrid_real_field_type(enum_field_types type)
    :Type_handler_hybrid_field_type(Type_handler::
                                    get_handler_by_real_type(type))
  { }
};


#endif /* SQL_TYPE_H_INCLUDED */
