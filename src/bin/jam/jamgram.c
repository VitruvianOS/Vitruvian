/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.3"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     _LANGLE_t = 258,
     _LANGLE_EQUALS_t = 259,
     _EQUALS_t = 260,
     _RANGLE_t = 261,
     _RANGLE_EQUALS_t = 262,
     _BAR_t = 263,
     _BARBAR_t = 264,
     _SEMIC_t = 265,
     _COLON_t = 266,
     _BANG_t = 267,
     _BANG_EQUALS_t = 268,
     _QUESTION_EQUALS_t = 269,
     _LPAREN_t = 270,
     _RPAREN_t = 271,
     _LBRACKET_t = 272,
     _RBRACKET_t = 273,
     _LBRACE_t = 274,
     _RBRACE_t = 275,
     _AMPER_t = 276,
     _AMPERAMPER_t = 277,
     _PLUS_EQUALS_t = 278,
     ACTIONS_t = 279,
     BIND_t = 280,
     BREAK_t = 281,
     CASE_t = 282,
     CONTINUE_t = 283,
     DEFAULT_t = 284,
     ELSE_t = 285,
     EXISTING_t = 286,
     FOR_t = 287,
     IF_t = 288,
     IGNORE_t = 289,
     IN_t = 290,
     INCLUDE_t = 291,
     JUMPTOEOF_t = 292,
     LOCAL_t = 293,
     MAXLINE_t = 294,
     ON_t = 295,
     PIECEMEAL_t = 296,
     QUIETLY_t = 297,
     RETURN_t = 298,
     RULE_t = 299,
     SWITCH_t = 300,
     TOGETHER_t = 301,
     UPDATED_t = 302,
     WHILE_t = 303,
     ARG = 304,
     STRING = 305
   };
#endif
/* Tokens.  */
#define _LANGLE_t 258
#define _LANGLE_EQUALS_t 259
#define _EQUALS_t 260
#define _RANGLE_t 261
#define _RANGLE_EQUALS_t 262
#define _BAR_t 263
#define _BARBAR_t 264
#define _SEMIC_t 265
#define _COLON_t 266
#define _BANG_t 267
#define _BANG_EQUALS_t 268
#define _QUESTION_EQUALS_t 269
#define _LPAREN_t 270
#define _RPAREN_t 271
#define _LBRACKET_t 272
#define _RBRACKET_t 273
#define _LBRACE_t 274
#define _RBRACE_t 275
#define _AMPER_t 276
#define _AMPERAMPER_t 277
#define _PLUS_EQUALS_t 278
#define ACTIONS_t 279
#define BIND_t 280
#define BREAK_t 281
#define CASE_t 282
#define CONTINUE_t 283
#define DEFAULT_t 284
#define ELSE_t 285
#define EXISTING_t 286
#define FOR_t 287
#define IF_t 288
#define IGNORE_t 289
#define IN_t 290
#define INCLUDE_t 291
#define JUMPTOEOF_t 292
#define LOCAL_t 293
#define MAXLINE_t 294
#define ON_t 295
#define PIECEMEAL_t 296
#define QUIETLY_t 297
#define RETURN_t 298
#define RULE_t 299
#define SWITCH_t 300
#define TOGETHER_t 301
#define UPDATED_t 302
#define WHILE_t 303
#define ARG 304
#define STRING 305




/* Copy the first part of user declarations.  */
#line 85 "jamgram.y"

#include "jam.h"

#include "lists.h"
#include "variable.h"
#include "parse.h"
#include "scan.h"
#include "compile.h"
#include "newstr.h"
#include "rules.h"

# define YYMAXDEPTH 10000	/* for OSF and other less endowed yaccs */

# define F0 (LIST *(*)(PARSE *, LOL *, int *))0
# define P0 (PARSE *)0
# define S0 (char *)0

# define pappend( l,r )    	parse_make( compile_append,l,r,P0,S0,S0,0 )
# define pbreak( l,f )     	parse_make( compile_break,l,P0,P0,S0,S0,f )
# define peval( c,l,r )		parse_make( compile_eval,l,r,P0,S0,S0,c )
# define pfor( s,l,r )    	parse_make( compile_foreach,l,r,P0,s,S0,0 )
# define pif( l,r,t )	  	parse_make( compile_if,l,r,t,S0,S0,0 )
# define pincl( l )       	parse_make( compile_include,l,P0,P0,S0,S0,0 )
# define plist( s )	  	parse_make( compile_list,P0,P0,P0,s,S0,0 )
# define plocal( l,r,t )  	parse_make( compile_local,l,r,t,S0,S0,0 )
# define pnull()	  	parse_make( compile_null,P0,P0,P0,S0,S0,0 )
# define pon( l,r )	  	parse_make( compile_on,l,r,P0,S0,S0,0 )
# define prule( a,p )     	parse_make( compile_rule,a,p,P0,S0,S0,0 )
# define prules( l,r )	  	parse_make( compile_rules,l,r,P0,S0,S0,0 )
# define pset( l,r,a ) 	  	parse_make( compile_set,l,r,P0,S0,S0,a )
# define pset1( l,r,t,a )	parse_make( compile_settings,l,r,t,S0,S0,a )
# define psetc( s,l,r )     	parse_make( compile_setcomp,l,r,P0,s,S0,0 )
# define psete( s,l,s1,f ) 	parse_make( compile_setexec,l,P0,P0,s,s1,f )
# define pswitch( l,r )   	parse_make( compile_switch,l,r,P0,S0,S0,0 )
# define pwhile( l,r )   	parse_make( compile_while,l,r,P0,S0,S0,0 )

# define pnode( l,r )    	parse_make( F0,l,r,P0,S0,S0,0 )
# define psnode( s,l )     	parse_make( F0,l,P0,P0,s,S0,0 )



/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef int YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 247 "y.tab.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int i)
#else
static int
YYID (i)
    int i;
#endif
{
  return i;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  41
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   270

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  51
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  21
/* YYNRULES -- Number of rules.  */
#define YYNRULES  74
/* YYNRULES -- Number of states.  */
#define YYNSTATES  157

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   305

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint8 yyprhs[] =
{
       0,     0,     3,     4,     6,     7,     9,    11,    14,    19,
      26,    30,    34,    38,    42,    47,    54,    58,    62,    66,
      74,    80,    86,    94,   100,   107,   111,   112,   113,   123,
     125,   127,   129,   132,   134,   138,   142,   146,   150,   154,
     158,   162,   166,   170,   174,   178,   181,   185,   186,   189,
     194,   195,   199,   201,   203,   207,   209,   210,   213,   215,
     216,   221,   224,   229,   234,   235,   238,   240,   242,   244,
     246,   248,   250,   253,   254
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      52,     0,    -1,    -1,    54,    -1,    -1,    54,    -1,    55,
      -1,    55,    54,    -1,    38,    64,    10,    53,    -1,    38,
      64,     5,    64,    10,    53,    -1,    19,    53,    20,    -1,
      36,    64,    10,    -1,    37,    64,    10,    -1,    66,    63,
      10,    -1,    66,    58,    64,    10,    -1,    66,    40,    64,
      58,    64,    10,    -1,    26,    64,    10,    -1,    28,    64,
      10,    -1,    43,    64,    10,    -1,    32,    49,    35,    64,
      19,    53,    20,    -1,    45,    64,    19,    60,    20,    -1,
      33,    59,    19,    53,    20,    -1,    33,    59,    19,    53,
      20,    30,    55,    -1,    48,    59,    19,    53,    20,    -1,
      44,    49,    62,    19,    53,    20,    -1,    40,    66,    55,
      -1,    -1,    -1,    24,    69,    49,    71,    19,    56,    50,
      57,    20,    -1,     5,    -1,    23,    -1,    14,    -1,    29,
       5,    -1,    66,    -1,    59,     5,    59,    -1,    59,    13,
      59,    -1,    59,     3,    59,    -1,    59,     4,    59,    -1,
      59,     6,    59,    -1,    59,     7,    59,    -1,    59,    21,
      59,    -1,    59,    22,    59,    -1,    59,     8,    59,    -1,
      59,     9,    59,    -1,    66,    35,    64,    -1,    12,    59,
      -1,    15,    59,    16,    -1,    -1,    61,    60,    -1,    27,
      49,    11,    53,    -1,    -1,    49,    11,    62,    -1,    49,
      -1,    64,    -1,    64,    11,    63,    -1,    65,    -1,    -1,
      65,    66,    -1,    49,    -1,    -1,    17,    67,    68,    18,
      -1,    66,    63,    -1,    40,    66,    66,    63,    -1,    40,
      66,    43,    64,    -1,    -1,    69,    70,    -1,    47,    -1,
      46,    -1,    34,    -1,    42,    -1,    41,    -1,    31,    -1,
      39,    49,    -1,    -1,    25,    64,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   128,   128,   130,   142,   143,   147,   149,   151,   153,
     157,   159,   161,   163,   165,   167,   169,   171,   173,   175,
     177,   179,   181,   183,   185,   187,   190,   192,   189,   201,
     203,   205,   207,   215,   217,   219,   221,   223,   225,   227,
     229,   231,   233,   235,   237,   239,   241,   252,   253,   257,
     267,   268,   270,   279,   281,   291,   296,   297,   301,   303,
     303,   312,   314,   316,   326,   327,   331,   333,   335,   337,
     339,   341,   343,   353,   354
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "_LANGLE_t", "_LANGLE_EQUALS_t",
  "_EQUALS_t", "_RANGLE_t", "_RANGLE_EQUALS_t", "_BAR_t", "_BARBAR_t",
  "_SEMIC_t", "_COLON_t", "_BANG_t", "_BANG_EQUALS_t",
  "_QUESTION_EQUALS_t", "_LPAREN_t", "_RPAREN_t", "_LBRACKET_t",
  "_RBRACKET_t", "_LBRACE_t", "_RBRACE_t", "_AMPER_t", "_AMPERAMPER_t",
  "_PLUS_EQUALS_t", "ACTIONS_t", "BIND_t", "BREAK_t", "CASE_t",
  "CONTINUE_t", "DEFAULT_t", "ELSE_t", "EXISTING_t", "FOR_t", "IF_t",
  "IGNORE_t", "IN_t", "INCLUDE_t", "JUMPTOEOF_t", "LOCAL_t", "MAXLINE_t",
  "ON_t", "PIECEMEAL_t", "QUIETLY_t", "RETURN_t", "RULE_t", "SWITCH_t",
  "TOGETHER_t", "UPDATED_t", "WHILE_t", "ARG", "STRING", "$accept", "run",
  "block", "rules", "rule", "@1", "@2", "assign", "expr", "cases", "case",
  "params", "lol", "list", "listp", "arg", "@3", "func", "eflags", "eflag",
  "bindlist", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    51,    52,    52,    53,    53,    54,    54,    54,    54,
      55,    55,    55,    55,    55,    55,    55,    55,    55,    55,
      55,    55,    55,    55,    55,    55,    56,    57,    55,    58,
      58,    58,    58,    59,    59,    59,    59,    59,    59,    59,
      59,    59,    59,    59,    59,    59,    59,    60,    60,    61,
      62,    62,    62,    63,    63,    64,    65,    65,    66,    67,
      66,    68,    68,    68,    69,    69,    70,    70,    70,    70,
      70,    70,    70,    71,    71
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     0,     1,     0,     1,     1,     2,     4,     6,
       3,     3,     3,     3,     4,     6,     3,     3,     3,     7,
       5,     5,     7,     5,     6,     3,     0,     0,     9,     1,
       1,     1,     2,     1,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     2,     3,     0,     2,     4,
       0,     3,     1,     1,     3,     1,     0,     2,     1,     0,
       4,     2,     4,     4,     0,     2,     1,     1,     1,     1,
       1,     1,     2,     0,     2
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       2,    59,     4,    64,    56,    56,     0,     0,    56,    56,
      56,     0,    56,     0,    56,     0,    58,     0,     3,     6,
      56,     0,     0,     5,     0,     0,    55,     0,     0,     0,
       0,     0,    33,     0,     0,     0,     0,     0,    50,     0,
       0,     1,     7,    29,    31,    30,     0,    56,    56,     0,
      53,     0,    56,     0,    10,    71,    68,     0,    70,    69,
      67,    66,    73,    65,    16,    57,    17,    56,    45,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     4,     0,
       0,    56,    11,    12,    56,     4,    25,    18,    52,     0,
      47,     4,    32,     0,     0,    13,    56,     0,    61,    60,
      72,    56,     0,     0,    46,    36,    37,    34,    38,    39,
      42,    43,    35,     0,    40,    41,    44,     0,     8,    50,
       4,     0,     0,    47,     0,    56,    14,    54,    56,    56,
      74,    26,     4,    21,     4,    51,     0,     0,    20,    48,
      23,     0,    63,    62,     0,     0,     0,     9,    24,     4,
      15,    27,    19,    22,    49,     0,    28
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,    17,    22,    23,    19,   144,   155,    48,    31,   122,
     123,    89,    49,    50,    26,    32,    21,    53,    24,    63,
     102
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -54
static const yytype_int16 yypact[] =
{
     124,   -54,   124,   -54,   -54,   -54,   -43,    -8,   -54,   -54,
     -54,    -7,   -54,   -26,   -54,    -8,   -54,    14,   -54,   124,
      10,   -12,     9,   -54,   221,    20,    -7,    21,    13,    -8,
      -8,   109,    17,    35,    61,    44,   157,    63,    23,    55,
     204,   -54,   -54,   -54,   -54,   -54,    71,   -54,   -54,    67,
      72,    -7,   -54,    64,   -54,   -54,   -54,    37,   -54,   -54,
     -54,   -54,    59,   -54,   -54,   -54,   -54,   -54,   -54,   224,
      -8,    -8,    -8,    -8,    -8,    -8,    -8,    -8,   124,    -8,
      -8,   -54,   -54,   -54,   -54,   124,   -54,   -54,    76,    69,
      66,   124,   -54,    30,    80,   -54,   -54,    -9,   -54,   -54,
     -54,   -54,    75,    87,   -54,   -54,   -54,    62,   -54,   -54,
     244,   244,    62,    90,    57,    57,   -54,    85,   -54,    23,
     124,    70,   101,    66,   103,   -54,   -54,   -54,   -54,   -54,
     -54,   -54,   124,    94,   124,   -54,   105,   115,   -54,   -54,
     -54,   117,   -54,   -54,    79,   118,   157,   -54,   -54,   124,
     -54,   -54,   -54,   -54,   -54,   119,   -54
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -54,   -54,   -53,    27,   -35,   -54,   -54,    42,    28,    19,
     -54,    18,   -49,     8,   -54,     0,   -54,   -54,   -54,   -54,
     -54
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint8 yytable[] =
{
      20,    86,    20,    98,    29,     1,    28,    30,     1,     1,
       1,    36,    25,    27,    41,    43,    33,    34,    35,    20,
      37,    52,    39,    38,    44,   113,    65,    18,    51,    54,
      64,    66,   118,    45,   128,    43,    20,    16,   124,    46,
      16,    16,    16,    40,    44,    82,    42,   127,    67,    84,
      47,    97,    81,    45,    85,    93,    94,    68,    69,    46,
      70,    71,    72,    73,    74,    70,    71,   136,    73,    74,
      77,    83,    88,    87,    90,   103,    92,    95,    20,   145,
     143,   147,    99,    96,   101,    20,   100,   119,   120,   116,
     126,    20,   117,   121,   131,   134,   154,   129,   105,   106,
     107,   108,   109,   110,   111,   112,   132,   114,   115,   130,
     133,   153,    70,    71,    72,    73,    74,    75,    76,   137,
      20,   138,    77,   140,   146,   148,   149,   150,    78,   151,
      79,    80,    20,   141,    20,   125,   142,   135,   152,   156,
       0,     1,   139,     2,     0,     0,    20,     0,     3,    20,
       4,     0,     5,     0,     0,     0,     6,     7,     0,     0,
       8,     9,    10,     0,    11,     0,     0,    12,    13,    14,
       0,     0,    15,    16,     1,     0,     2,     0,     0,     0,
       0,     3,     0,     4,     0,     5,     0,     0,     0,     6,
       7,     0,     0,     8,     9,     0,     0,    11,     0,     0,
      12,    13,    14,     0,     0,    15,    16,    70,    71,    72,
      73,    74,    75,    76,     0,     0,     0,    77,     0,     0,
       0,     0,     0,    91,     0,    79,    80,    70,    71,    72,
      73,    74,    75,    76,     0,     0,     0,    77,     0,     0,
     104,     0,     0,     0,     0,    79,    80,    70,    71,    72,
      73,    74,    55,     0,     0,    56,     0,    77,     0,     0,
      57,     0,    58,    59,     0,    79,    80,    60,    61,     0,
      62
};

static const yytype_int16 yycheck[] =
{
       0,    36,     2,    52,    12,    17,    49,    15,    17,    17,
      17,    11,     4,     5,     0,     5,     8,     9,    10,    19,
      12,    21,    14,    49,    14,    78,    26,     0,    40,    20,
      10,    10,    85,    23,    43,     5,    36,    49,    91,    29,
      49,    49,    49,    15,    14,    10,    19,    96,    35,     5,
      40,    51,    35,    23,    10,    47,    48,    29,    30,    29,
       3,     4,     5,     6,     7,     3,     4,   120,     6,     7,
      13,    10,    49,    10,    19,    67,     5,    10,    78,   132,
     129,   134,    18,    11,    25,    85,    49,    11,    19,    81,
      10,    91,    84,    27,    19,    10,   149,    97,    70,    71,
      72,    73,    74,    75,    76,    77,    19,    79,    80,   101,
      20,   146,     3,     4,     5,     6,     7,     8,     9,    49,
     120,    20,    13,    20,    30,    20,    11,    10,    19,    50,
      21,    22,   132,   125,   134,    93,   128,   119,    20,    20,
      -1,    17,   123,    19,    -1,    -1,   146,    -1,    24,   149,
      26,    -1,    28,    -1,    -1,    -1,    32,    33,    -1,    -1,
      36,    37,    38,    -1,    40,    -1,    -1,    43,    44,    45,
      -1,    -1,    48,    49,    17,    -1,    19,    -1,    -1,    -1,
      -1,    24,    -1,    26,    -1,    28,    -1,    -1,    -1,    32,
      33,    -1,    -1,    36,    37,    -1,    -1,    40,    -1,    -1,
      43,    44,    45,    -1,    -1,    48,    49,     3,     4,     5,
       6,     7,     8,     9,    -1,    -1,    -1,    13,    -1,    -1,
      -1,    -1,    -1,    19,    -1,    21,    22,     3,     4,     5,
       6,     7,     8,     9,    -1,    -1,    -1,    13,    -1,    -1,
      16,    -1,    -1,    -1,    -1,    21,    22,     3,     4,     5,
       6,     7,    31,    -1,    -1,    34,    -1,    13,    -1,    -1,
      39,    -1,    41,    42,    -1,    21,    22,    46,    47,    -1,
      49
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    17,    19,    24,    26,    28,    32,    33,    36,    37,
      38,    40,    43,    44,    45,    48,    49,    52,    54,    55,
      66,    67,    53,    54,    69,    64,    65,    64,    49,    12,
      15,    59,    66,    64,    64,    64,    66,    64,    49,    64,
      59,     0,    54,     5,    14,    23,    29,    40,    58,    63,
      64,    40,    66,    68,    20,    31,    34,    39,    41,    42,
      46,    47,    49,    70,    10,    66,    10,    35,    59,    59,
       3,     4,     5,     6,     7,     8,     9,    13,    19,    21,
      22,    35,    10,    10,     5,    10,    55,    10,    49,    62,
      19,    19,     5,    64,    64,    10,    11,    66,    63,    18,
      49,    25,    71,    64,    16,    59,    59,    59,    59,    59,
      59,    59,    59,    53,    59,    59,    64,    64,    53,    11,
      19,    27,    60,    61,    53,    58,    10,    63,    43,    66,
      64,    19,    19,    20,    10,    62,    53,    49,    20,    60,
      20,    64,    64,    63,    56,    53,    30,    53,    20,    11,
      10,    50,    20,    55,    53,    57,    20
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (YYLEX_PARAM)
#else
# define YYLEX yylex ()
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *bottom, yytype_int16 *top)
#else
static void
yy_stack_print (bottom, top)
    yytype_int16 *bottom;
    yytype_int16 *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yyrule)
    YYSTYPE *yyvsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       		       );
      fprintf (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, Rule); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yymsg, yytype, yyvaluep)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  YYUSE (yyvaluep);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {

      default:
	break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */



/* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;



/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  
  int yystate;
  int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;
#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  yytype_int16 yyssa[YYINITDEPTH];
  yytype_int16 *yyss = yyssa;
  yytype_int16 *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);

#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;


      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     look-ahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to look-ahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;

  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 3:
#line 131 "jamgram.y"
    { parse_save( (yyvsp[(1) - (1)]).parse ); }
    break;

  case 4:
#line 142 "jamgram.y"
    { (yyval).parse = pnull(); }
    break;

  case 5:
#line 144 "jamgram.y"
    { (yyval).parse = (yyvsp[(1) - (1)]).parse; }
    break;

  case 6:
#line 148 "jamgram.y"
    { (yyval).parse = (yyvsp[(1) - (1)]).parse; }
    break;

  case 7:
#line 150 "jamgram.y"
    { (yyval).parse = prules( (yyvsp[(1) - (2)]).parse, (yyvsp[(2) - (2)]).parse ); }
    break;

  case 8:
#line 152 "jamgram.y"
    { (yyval).parse = plocal( (yyvsp[(2) - (4)]).parse, pnull(), (yyvsp[(4) - (4)]).parse ); }
    break;

  case 9:
#line 154 "jamgram.y"
    { (yyval).parse = plocal( (yyvsp[(2) - (6)]).parse, (yyvsp[(4) - (6)]).parse, (yyvsp[(6) - (6)]).parse ); }
    break;

  case 10:
#line 158 "jamgram.y"
    { (yyval).parse = (yyvsp[(2) - (3)]).parse; }
    break;

  case 11:
#line 160 "jamgram.y"
    { (yyval).parse = pincl( (yyvsp[(2) - (3)]).parse ); }
    break;

  case 12:
#line 162 "jamgram.y"
    { (yyval).parse = pbreak( (yyvsp[(2) - (3)]).parse, JMP_EOF ); }
    break;

  case 13:
#line 164 "jamgram.y"
    { (yyval).parse = prule( (yyvsp[(1) - (3)]).parse, (yyvsp[(2) - (3)]).parse ); }
    break;

  case 14:
#line 166 "jamgram.y"
    { (yyval).parse = pset( (yyvsp[(1) - (4)]).parse, (yyvsp[(3) - (4)]).parse, (yyvsp[(2) - (4)]).number ); }
    break;

  case 15:
#line 168 "jamgram.y"
    { (yyval).parse = pset1( (yyvsp[(1) - (6)]).parse, (yyvsp[(3) - (6)]).parse, (yyvsp[(5) - (6)]).parse, (yyvsp[(4) - (6)]).number ); }
    break;

  case 16:
#line 170 "jamgram.y"
    { (yyval).parse = pbreak( (yyvsp[(2) - (3)]).parse, JMP_BREAK ); }
    break;

  case 17:
#line 172 "jamgram.y"
    { (yyval).parse = pbreak( (yyvsp[(2) - (3)]).parse, JMP_CONTINUE ); }
    break;

  case 18:
#line 174 "jamgram.y"
    { (yyval).parse = pbreak( (yyvsp[(2) - (3)]).parse, JMP_RETURN ); }
    break;

  case 19:
#line 176 "jamgram.y"
    { (yyval).parse = pfor( (yyvsp[(2) - (7)]).string, (yyvsp[(4) - (7)]).parse, (yyvsp[(6) - (7)]).parse ); }
    break;

  case 20:
#line 178 "jamgram.y"
    { (yyval).parse = pswitch( (yyvsp[(2) - (5)]).parse, (yyvsp[(4) - (5)]).parse ); }
    break;

  case 21:
#line 180 "jamgram.y"
    { (yyval).parse = pif( (yyvsp[(2) - (5)]).parse, (yyvsp[(4) - (5)]).parse, pnull() ); }
    break;

  case 22:
#line 182 "jamgram.y"
    { (yyval).parse = pif( (yyvsp[(2) - (7)]).parse, (yyvsp[(4) - (7)]).parse, (yyvsp[(7) - (7)]).parse ); }
    break;

  case 23:
#line 184 "jamgram.y"
    { (yyval).parse = pwhile( (yyvsp[(2) - (5)]).parse, (yyvsp[(4) - (5)]).parse ); }
    break;

  case 24:
#line 186 "jamgram.y"
    { (yyval).parse = psetc( (yyvsp[(2) - (6)]).string, (yyvsp[(3) - (6)]).parse, (yyvsp[(5) - (6)]).parse ); }
    break;

  case 25:
#line 188 "jamgram.y"
    { (yyval).parse = pon( (yyvsp[(2) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 26:
#line 190 "jamgram.y"
    { yymode( SCAN_STRING ); }
    break;

  case 27:
#line 192 "jamgram.y"
    { yymode( SCAN_NORMAL ); }
    break;

  case 28:
#line 194 "jamgram.y"
    { (yyval).parse = psete( (yyvsp[(3) - (9)]).string,(yyvsp[(4) - (9)]).parse,(yyvsp[(7) - (9)]).string,(yyvsp[(2) - (9)]).number ); }
    break;

  case 29:
#line 202 "jamgram.y"
    { (yyval).number = VAR_SET; }
    break;

  case 30:
#line 204 "jamgram.y"
    { (yyval).number = VAR_APPEND; }
    break;

  case 31:
#line 206 "jamgram.y"
    { (yyval).number = VAR_DEFAULT; }
    break;

  case 32:
#line 208 "jamgram.y"
    { (yyval).number = VAR_DEFAULT; }
    break;

  case 33:
#line 216 "jamgram.y"
    { (yyval).parse = peval( EXPR_EXISTS, (yyvsp[(1) - (1)]).parse, pnull() ); }
    break;

  case 34:
#line 218 "jamgram.y"
    { (yyval).parse = peval( EXPR_EQUALS, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 35:
#line 220 "jamgram.y"
    { (yyval).parse = peval( EXPR_NOTEQ, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 36:
#line 222 "jamgram.y"
    { (yyval).parse = peval( EXPR_LESS, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 37:
#line 224 "jamgram.y"
    { (yyval).parse = peval( EXPR_LESSEQ, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 38:
#line 226 "jamgram.y"
    { (yyval).parse = peval( EXPR_MORE, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 39:
#line 228 "jamgram.y"
    { (yyval).parse = peval( EXPR_MOREEQ, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 40:
#line 230 "jamgram.y"
    { (yyval).parse = peval( EXPR_AND, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 41:
#line 232 "jamgram.y"
    { (yyval).parse = peval( EXPR_AND, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 42:
#line 234 "jamgram.y"
    { (yyval).parse = peval( EXPR_OR, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 43:
#line 236 "jamgram.y"
    { (yyval).parse = peval( EXPR_OR, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 44:
#line 238 "jamgram.y"
    { (yyval).parse = peval( EXPR_IN, (yyvsp[(1) - (3)]).parse, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 45:
#line 240 "jamgram.y"
    { (yyval).parse = peval( EXPR_NOT, (yyvsp[(2) - (2)]).parse, pnull() ); }
    break;

  case 46:
#line 242 "jamgram.y"
    { (yyval).parse = (yyvsp[(2) - (3)]).parse; }
    break;

  case 47:
#line 252 "jamgram.y"
    { (yyval).parse = P0; }
    break;

  case 48:
#line 254 "jamgram.y"
    { (yyval).parse = pnode( (yyvsp[(1) - (2)]).parse, (yyvsp[(2) - (2)]).parse ); }
    break;

  case 49:
#line 258 "jamgram.y"
    { (yyval).parse = psnode( (yyvsp[(2) - (4)]).string, (yyvsp[(4) - (4)]).parse ); }
    break;

  case 50:
#line 267 "jamgram.y"
    { (yyval).parse = P0; }
    break;

  case 51:
#line 269 "jamgram.y"
    { (yyval).parse = psnode( (yyvsp[(1) - (3)]).string, (yyvsp[(3) - (3)]).parse ); }
    break;

  case 52:
#line 271 "jamgram.y"
    { (yyval).parse = psnode( (yyvsp[(1) - (1)]).string, P0 ); }
    break;

  case 53:
#line 280 "jamgram.y"
    { (yyval).parse = pnode( P0, (yyvsp[(1) - (1)]).parse ); }
    break;

  case 54:
#line 282 "jamgram.y"
    { (yyval).parse = pnode( (yyvsp[(3) - (3)]).parse, (yyvsp[(1) - (3)]).parse ); }
    break;

  case 55:
#line 292 "jamgram.y"
    { (yyval).parse = (yyvsp[(1) - (1)]).parse; yymode( SCAN_NORMAL ); }
    break;

  case 56:
#line 296 "jamgram.y"
    { (yyval).parse = pnull(); yymode( SCAN_PUNCT ); }
    break;

  case 57:
#line 298 "jamgram.y"
    { (yyval).parse = pappend( (yyvsp[(1) - (2)]).parse, (yyvsp[(2) - (2)]).parse ); }
    break;

  case 58:
#line 302 "jamgram.y"
    { (yyval).parse = plist( (yyvsp[(1) - (1)]).string ); }
    break;

  case 59:
#line 303 "jamgram.y"
    { yymode( SCAN_NORMAL ); }
    break;

  case 60:
#line 304 "jamgram.y"
    { (yyval).parse = (yyvsp[(3) - (4)]).parse; }
    break;

  case 61:
#line 313 "jamgram.y"
    { (yyval).parse = prule( (yyvsp[(1) - (2)]).parse, (yyvsp[(2) - (2)]).parse ); }
    break;

  case 62:
#line 315 "jamgram.y"
    { (yyval).parse = pon( (yyvsp[(2) - (4)]).parse, prule( (yyvsp[(3) - (4)]).parse, (yyvsp[(4) - (4)]).parse ) ); }
    break;

  case 63:
#line 317 "jamgram.y"
    { (yyval).parse = pon( (yyvsp[(2) - (4)]).parse, (yyvsp[(4) - (4)]).parse ); }
    break;

  case 64:
#line 326 "jamgram.y"
    { (yyval).number = 0; }
    break;

  case 65:
#line 328 "jamgram.y"
    { (yyval).number = (yyvsp[(1) - (2)]).number | (yyvsp[(2) - (2)]).number; }
    break;

  case 66:
#line 332 "jamgram.y"
    { (yyval).number = RULE_UPDATED; }
    break;

  case 67:
#line 334 "jamgram.y"
    { (yyval).number = RULE_TOGETHER; }
    break;

  case 68:
#line 336 "jamgram.y"
    { (yyval).number = RULE_IGNORE; }
    break;

  case 69:
#line 338 "jamgram.y"
    { (yyval).number = RULE_QUIETLY; }
    break;

  case 70:
#line 340 "jamgram.y"
    { (yyval).number = RULE_PIECEMEAL; }
    break;

  case 71:
#line 342 "jamgram.y"
    { (yyval).number = RULE_EXISTING; }
    break;

  case 72:
#line 344 "jamgram.y"
    { (yyval).number = atoi( (yyvsp[(2) - (2)]).string ) * RULE_MAXLINE; }
    break;

  case 73:
#line 353 "jamgram.y"
    { (yyval).parse = pnull(); }
    break;

  case 74:
#line 355 "jamgram.y"
    { (yyval).parse = (yyvsp[(2) - (2)]).parse; }
    break;


/* Line 1267 of yacc.c.  */
#line 1963 "y.tab.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;


  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse look-ahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;


      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}



