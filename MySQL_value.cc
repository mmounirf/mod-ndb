/* Copyright (C) 2006 - 2009 Sun Microsystems
 All rights reserved. Use is subject to license terms.

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
*/



/* MySQL::value:
   take an ASCII value "val", and encode it properly for NDB so that it can be 
   stored in (or compared against) column "col"
 */


/* 
   This file has a different set of includes than other mod_ndb source files:
   it uses my_global.h to get MySQL's typedefs and macros like "sint3korr",
   but it does not include mod_ndb.h (because you can get trouble if you try 
   to combine mysql headers and apache headers in a single source file).
*/

#include <strings.h>
#include <ctype.h>
#include "my_global.h"
#include "mysql.h"
#include "NdbApi.hpp"
#include "httpd.h"
#include "http_config.h"
#include "mod_ndb_compat.h"
#include "mod_ndb_debug.h"
#include "result_buffer.h"
#include "MySQL_value.h"

// Apache might have disabled strtoul()
#ifdef strtoul
#undef strtoul
#endif

uint64_t flip64(uint64_t i) {
  uint64_t h1 = (i & 0xffffffff00000000LL) >> 32 ;
  uint64_t h2 = (i & 0x00000000ffffffffLL) << 32 ;
  return ( h1 | h2) ; 
}


void MySQL::value(mvalue &m, ap_pool *p, 
                  const NdbDictionary::Column *col, const char *val) 
{
  const unsigned short s_lo = 255;
  const unsigned short s_hi = 65535 ^ 255; 
  unsigned char len;
  unsigned short s_len;
  unsigned int l_len;
  char *s, *q;
  int aux_int;

  if(col == 0) {
    m.use_value = err_bad_column;
    return;
  }

  NdbDictionary::Column::Type col_type = col->getType();
    
  bool is_char_col = 
    ( (col_type == NdbDictionary::Column::Varchar) ||
      (col_type == NdbDictionary::Column::Longvarchar) ||
      (col_type == NdbDictionary::Column::Char));        

  m.ndb_column = col;
  
  /* String columns */
  if(is_char_col) {
  
    if(! val) { /* null pointer */
      m.use_value = use_null;
      m.u.val_64 = 0;
      return;
    }
  
    switch(col_type) {
      /* "If the attribute is of variable size, its value must start with
      1 or 2 little-endian length bytes"   [ i.e. LSB first ]*/
      
      case NdbDictionary::Column::Varchar:      
        COV_point("varchar");
        m.len = len = (unsigned char) strlen(val);
        if(len > col->getLength()) len = (unsigned char) col->getLength();
          m.u.val_char = (char *) ap_palloc(p, len + 2);
        * m.u.val_char = len;
        ap_cpystrn(m.u.val_char+1, val, len+1);
        m.use_value = use_char; 
        m.col_len = col->getLength() + 1;
        return;
        
      case NdbDictionary::Column::Longvarchar:
        COV_point("longvarchar");
        m.len = s_len = strlen(val);
        if(s_len > col->getLength()) s_len = col->getLength();
          m.u.val_char = (char *) ap_palloc(p, s_len + 3);
        * m.u.val_char     = (char) (s_len & s_lo);
        * (m.u.val_char+1) = (char) ((s_len & s_hi) >> 8);
        ap_cpystrn(m.u.val_char+2, val, s_len+1);
        m.use_value = use_char; 
        m.col_len = col->getLength() + 2;
        return;
        
      case NdbDictionary::Column::Char:
        COV_point("char");
       // Copy the value into the buffer, then right-pad with spaces
        m.len = l_len = strlen(val);
        if(l_len > (unsigned) col->getLength()) l_len = col->getLength();
          m.u.val_char = (char *) ap_palloc(p, col->getLength() + 1);
        strcpy(m.u.val_char, val);
        s = m.u.val_char + l_len;
        q = m.u.val_char + col->getLength();
        while (s < q) *s++ = ' ';
          *q = 0;      
        m.use_value = use_char;
        m.col_len = col->getLength();
        return;
        
      default:
        assert(0);
    }
  }

  /* Date columns */
  if ( (col_type == NdbDictionary::Column::Time) ||
       (col_type == NdbDictionary::Column::Date) ||
       (col_type == NdbDictionary::Column::Datetime)) {
    MYSQL_TIME tm;
    char strbuf[64];
    char *buf = strbuf;
    const char *c = val;
    int yymmdd;
    
    if(! val) { /* null pointer */
      m.use_value = use_null;
      m.u.val_64 = 0;
      return;
    }
    /* Parse a MySQL date, time, or datetime.  Allow it to be signed.
       Treat non-digits as separators, and then treat the rest 
       as a number. */
    if(*c == '-' || *c == '+') *buf++ = *c++;
    for(register int i = 0 ; i < 62 && *c != 0 ; c++, i++ ) 
        if(isdigit(*c))
            *buf++ = *c; 
    *buf = 0;
    buf = strbuf;

    switch(col_type) {
      case NdbDictionary::Column::Datetime :
        COV_point("datetime");
        m.u.val_unsigned_64 = strtoull(buf, 0, 10);
        m.use_value = use_unsigned_64;
        return;
      case NdbDictionary::Column::Time :
        COV_point("time");
        m.use_value = use_signed;
        aux_int = strtol(buf, 0, 10);
        store24(m.u.val_signed, aux_int);
        return;
      case NdbDictionary::Column::Date :
       COV_point("date");
       bzero(&tm, sizeof(MYSQL_TIME));
        yymmdd = strtol(buf, 0, 10);
        tm.year = yymmdd/10000 % 10000;
        tm.month  = yymmdd/100 % 100;
        tm.day = yymmdd % 100;  
        aux_int = (tm.year << 9) | (tm.month << 5) | tm.day;
        m.use_value = use_signed;
        store24(m.u.val_signed, aux_int);
        return;
      default:
        assert(0);
    }
  }

  /* Numeric columns */  
  if(! val) {  // You can't do anything with a null pointer
    m.use_value = err_bad_user_value;
    return;
  }

  /* Dynamic values @++. @--. @null, @time, @autoinc */
  if(*val == '@') {
    if(!strcmp(val,"@null")) {
      COV_point("@null");
      m.use_value = use_null;
      m.u.val_64 = 0;
      return;
    }
    if(!strcmp(val,"@++")) {
      COV_point("@++");
      m.use_value = use_interpreted;
      m.interpreted = is_increment;
      return;
    }
    if(!strcmp(val,"@--")) {
      COV_point("@--");
      m.use_value = use_interpreted;
      m.interpreted = is_decrement;
      return;
    }
    if(!strcmp(val,"@time")) {
      COV_point("@time");
     m.use_value = use_unsigned;
      time(& m.u.val_time);
      return;
    }
    if(!strcmp(val,"@autoinc")) {
      COV_point("@autoinc");
      m.use_value = use_autoinc;
      if(col_type == NdbDictionary::Column::Bigint 
       || col_type == NdbDictionary::Column::Bigunsigned)
        m.len = 8;
      else m.len = 4;
      return;
    }
  }
  
  switch(col_type) {    
    case NdbDictionary::Column::Int:
      COV_point("int");
      m.use_value = use_signed;
      m.u.val_signed = atoi(val);
      return;
      
    case NdbDictionary::Column::Unsigned:
    case NdbDictionary::Column::Timestamp:
      COV_point("unsigned");
      m.use_value = use_unsigned;
      m.u.val_unsigned = strtoul(val,0,0);
      return;
      
    case NdbDictionary::Column::Float:
      COV_point("float");
      m.use_value = use_float;
      m.u.val_float = atof(val);
      return;
      
    case NdbDictionary::Column::Double:
      COV_point("double");
      m.use_value = use_double;
      m.u.val_double = strtod(val,0);
      return;
      
    case NdbDictionary::Column::Bigint:
      COV_point("bigint");
      m.use_value = use_64;
      m.u.val_64 = strtoll(val,0,0);
      return;
      
    case NdbDictionary::Column::Bigunsigned:
      COV_point("bigunsigned");
      m.use_value = use_unsigned_64;
      m.u.val_unsigned_64 = strtoull(val,0,0);
      return;

    case NdbDictionary::Column::Bit:
      COV_point("bit");
      m.use_value = use_unsigned_64;
      m.u.val_unsigned_64 = ndbapi_bit_flip(strtoull(val,0,0));
      return; 
        
    /* Tiny, small, and medium types -- be like mysql: 
        on overflow, put in the highest allowed value */
    case NdbDictionary::Column::Tinyint:
      COV_point("tinyint");
      m.use_value = use_signed;
      aux_int = strtol(val,0,0);
      if(aux_int < -128) aux_int = -128 , m.over = 1;
      else if( aux_int > 127) aux_int = 127, m.over = 1;
      m.u.val_8 = (char) aux_int;
      return;

    case NdbDictionary::Column::Tinyunsigned:
      COV_point("tinyunsigned");
      m.use_value = use_unsigned;
      aux_int = strtol(val,0,0);
      if(aux_int > 255) aux_int = 255 , m.over = 1;
      else if (aux_int < 0) aux_int = 0 , m.over = 1;
      m.u.val_unsigned_8 = (unsigned char) aux_int;
      return;

    case NdbDictionary::Column::Smallint:
      COV_point("smallint");
      m.use_value = use_signed;
      aux_int = strtol(val,0,0);
      if(aux_int < -32768) aux_int = -32768 , m.over = 1;
      else if(aux_int > 32767)  aux_int = 32767 , m.over = 1;
      m.u.val_16 = (int16_t) aux_int;
      return;

    case NdbDictionary::Column::Smallunsigned:
      COV_point("smallunsigned");
      m.use_value = use_unsigned;
      aux_int = strtol(val,0,0);
      if(aux_int > 65535) aux_int = 65535 , m.over = 1;
      else if (aux_int < 0) aux_int = 0 , m.over = 1;
      m.u.val_unsigned_16 = (Uint16) aux_int;
      return;

    case NdbDictionary::Column::Mediumint:
      COV_point("mediumint");
      m.use_value = use_signed;
      aux_int = strtol(val,0,0);
      if(aux_int > 8388607) aux_int = 8388607 , m.over = 1;
      else if(aux_int < -8388608) aux_int = -8388608 , m.over = 1;
      store24(m.u.val_signed, aux_int);
      return;

    case NdbDictionary::Column::Mediumunsigned:
      COV_point("mediumunsigned");
      m.use_value = use_unsigned;
      aux_int = strtol(val,0,0);
      if(aux_int > 16777215) aux_int = 16777215 , m.over = 1;
      else if (aux_int < 0) aux_int = 0 , m.over = 1;
      store24(m.u.val_unsigned, aux_int);
      return;

    case NdbDictionary::Column::Year:
      COV_point("year");
      m.use_value = use_unsigned;
      aux_int = strtol(val,0,0) - 1900;      
      m.u.val_unsigned_8 = (unsigned char) aux_int;
      return;

    case NdbDictionary::Column::Decimal:
    case NdbDictionary::Column::Decimalunsigned:
    {                                  // these braces prevent a compiler error
      COV_point("decimal");
      decimal_digit_t digits[DECIMAL_BUFF]; 
      char *end = (char *) val + strlen(val);
      const int prec  = col->getPrecision();
      const int scale = col->getScale();
      decimal_t dec = {prec - scale, scale, DECIMAL_BUFF ,0, digits};

      string2decimal(val, &dec, &end);
      m.use_value = use_char;
      m.col_len = 0;   /* For NdbScanFilter::cmp() */
      /* decimal_bin_size() is never greater than 32: */
      m.u.val_char = (char *) ap_pcalloc(p, 32);
      decimal2bin(&dec, m.u.val_char, prec, scale);
     }
      return;

    case NdbDictionary::Column::Text:
    case NdbDictionary::Column::Blob:    
    case NdbDictionary::Column::Longvarbinary:
    case NdbDictionary::Column::Varbinary:
    case NdbDictionary::Column::Binary:
      COV_point("set must_use_binary");
      m.use_value = must_use_binary;
      return;

    /* not implemented */
    /* Olddecimal types are just strings.  But you cannot create old decimal
       columns with MySQL 5, so this is difficult to test. */
    case NdbDictionary::Column::Olddecimal:
    case NdbDictionary::Column::Olddecimalunsigned:
    default:
      COV_point("bad data type");
      m.use_value = err_bad_data_type;
      return;
  }
}


/* binary_value(): Set a binary value.  
 */

void MySQL::binary_value(mvalue &m, ap_pool *p, const NdbDictionary::Column *col, 
                         len_string *value) 
{
  const unsigned short s_lo = 255;
  const unsigned short s_hi = 65535 ^ 255; 
  unsigned char   c_len = 0;
  unsigned short  s_len = 0;
  
  m.col_len = col->getLength();
  m.len = (value->len < m.col_len) ? value->len : m.col_len;
  
  switch(col->getType()) {    
 
      case NdbDictionary::Column::Text:
      case NdbDictionary::Column::Blob:                   
        COV_point("BLOB");
        m.use_value = use_blob;
        m.binary_info = value;
        return;
        
      case NdbDictionary::Column::Binary:
        COV_point("Binary");
        m.use_value = use_char;
        m.u.val_char = (char *) ap_pcalloc(p, m.col_len);
        memcpy(m.u.val_char, value->string, m.len);      
        return;
        
      case NdbDictionary::Column::Varbinary:
        COV_point("Varbinary");
        m.use_value = use_char;
        m.col_len += 1;                     /* 1 length-byte */
        c_len = m.len;                      /* 8-bit unsigned char */
        m.u.val_char = (char *) ap_palloc(p, m.len + 1);
        * m.u.val_char = c_len;             /* set the length byte */
        memcpy(m.u.val_char+1, value->string, m.len);      
        return;
        
      case NdbDictionary::Column::Longvarbinary:
        COV_point("Longvarbinary");
        m.use_value = use_char;
        m.col_len += 2;                      /* 2 length-bytes */ 
        s_len = m.len;                       /* 16-bit unsigned short  */
        m.u.val_char = (char *) ap_palloc(p, s_len + 2);
        * m.u.val_char     = (char) (s_len & s_lo);
        * (m.u.val_char+1) = (char) ((s_len & s_hi) >> 8);
        ap_cpystrn(m.u.val_char+2, value->string, s_len+1);
        return;
        
      default:
        m.use_value = err_bad_data_type;
        return;
  }
}


