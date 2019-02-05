//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  ascii.h
//

#if !defined(_ASCII_H)

// ASCII codes with special names
#define ASCII_NULL			0	// Ctrl-@
#define ASCII_SOH			1	// Ctrl-A
#define ASCII_STX			2	// Ctrl-B
#define ASCII_ETX			3	// Ctrl-C
#define ASCII_EOT			4	// Ctrl-D
#define ASCII_ENQ			5	// Ctrl-E
#define ASCII_ACK			6	// Ctrl-F
#define ASCII_BEL			7	// Ctrl-G
#define ASCII_BS			8	// Ctrl-H
#define ASCII_HT			9	// Ctrl-I
#define ASCII_LF			10	// Ctrl-J
#define ASCII_VT			11	// Ctrl-K
#define ASCII_FF			12	// Ctrl-L
#define ASCII_CR			13	// Ctrl-M
#define ASCII_SO			14	// Ctrl-N
#define ASCII_SI			15	// Ctrl-O
#define ASCII_DLE			16	// Ctrl-P
#define ASCII_DC1			17	// Ctrl-Q
#define ASCII_DC2			18	// Ctrl-R
#define ASCII_DC3			19	// Ctrl-S
#define ASCII_DC4			20	// Ctrl-T
#define ASCII_NAK			21	// Ctrl-U
#define ASCII_SYN			22	// Ctrl-V
#define ASCII_ETB			23	// Ctrl-W
#define ASCII_CAN			24	// Ctrl-X
#define ASCII_EM			25	// Ctrl-Y
#define ASCII_SUB			26	// Ctrl-Z
#define ASCII_ESC			27	// Ctrl-[
#define ASCII_FS			28	// Ctrl-backslash
#define ASCII_GS			29	// Ctrl-]
#define ASCII_RS			30	// Ctrl-^
#define ASCII_US			31	// Ctrl-_

// Aliases
#define ASCII_ENDOFFILE		ASCII_EOT
#define ASCII_BACKSPACE		ASCII_BS
#define ASCII_TAB			ASCII_HT
#define ASCII_ENTER			ASCII_LF
#define ASCII_SPACE			32
#define ASCII_DEL			127

// These are unoffical overrides of certain codes that we use for things
// that don't have ASCII codes
#define ASCII_PAGEUP		ASCII_VT
#define ASCII_PAGEDOWN		ASCII_FF
#define ASCII_HOME			ASCII_CR
#define ASCII_CRSRUP		ASCII_DC1
#define ASCII_CRSRLEFT		ASCII_DC2
#define ASCII_CRSRRIGHT		ASCII_DC3
#define ASCII_CRSRDOWN		ASCII_DC4

#define ASCII_CHARS			255
#define ASCII_PRINTABLES	((ASCII_DEL - ASCII_SPACE) + 1)	// 96

/*
                               ASCII CODES
    Oct   Dec   Hex   Char                        Oct   Dec   Hex   Char
    ------------------------------------------------------------------------
    000   0     00    NUL '\0'                    100   64    40    @
    001   1     01    SOH (start of heading)      101   65    41    A
    002   2     02    STX (start of text)         102   66    42    B
    003   3     03    ETX (end of text)           103   67    43    C
    004   4     04    EOT (end of transmission)   104   68    44    D
    005   5     05    ENQ (enquiry)               105   69    45    E
    006   6     06    ACK (acknowledge)           106   70    46    F
    007   7     07    BEL '\a' (bell)             107   71    47    G
    010   8     08    BS  '\b' (backspace)        110   72    48    H
    011   9     09    HT  '\t' (horizontal tab)   111   73    49    I
    012   10    0A    LF  '\n' (new line)         112   74    4A    J
    013   11    0B    VT  '\v' (vertical tab)     113   75    4B    K
    014   12    0C    FF  '\f' (form feed)        114   76    4C    L
    015   13    0D    CR  '\r' (carriage ret)     115   77    4D    M
    016   14    0E    SO  (shift out)             116   78    4E    N
    017   15    0F    SI  (shift in)              117   79    4F    O
    020   16    10    DLE (data link escape)      120   80    50    P
    021   17    11    DC1 (device control 1)      121   81    51    Q
    022   18    12    DC2 (device control 2)      122   82    52    R
    023   19    13    DC3 (device control 3)      123   83    53    S
    024   20    14    DC4 (device control 4)      124   84    54    T
    025   21    15    NAK (negative ack.)         125   85    55    U
    026   22    16    SYN (synchronous idle)      126   86    56    V
    027   23    17    ETB (end of trans. blk)     127   87    57    W
    030   24    18    CAN (cancel)                130   88    58    X
    031   25    19    EM  (end of medium)         131   89    59    Y
    032   26    1A    SUB (substitute)            132   90    5A    Z
    033   27    1B    ESC (escape)                133   91    5B    [
    034   28    1C    FS  (file separator)        134   92    5C    \  '\\'
    035   29    1D    GS  (group separator)       135   93    5D    ]
    036   30    1E    RS  (record separator)      136   94    5E    ^
    037   31    1F    US  (unit separator)        137   95    5F    _
    040   32    20    SPACE                       140   96    60    `
    041   33    21    !                           141   97    61    a
    042   34    22    "                           142   98    62    b
    043   35    23    #                           143   99    63    c
    044   36    24    $                           144   100   64    d
    045   37    25    %                           145   101   65    e
    046   38    26    &                           146   102   66    f
    047   39    27    '                           147   103   67    g
    050   40    28    (                           150   104   68    h
    051   41    29    )                           151   105   69    i
    052   42    2A    *                           152   106   6A    j
    053   43    2B    +                           153   107   6B    k
    054   44    2C    ,                           154   108   6C    l
    055   45    2D    -                           155   109   6D    m
    056   46    2E    .                           156   110   6E    n
    057   47    2F    /                           157   111   6F    o
    060   48    30    0                           160   112   70    p
    061   49    31    1                           161   113   71    q
    062   50    32    2                           162   114   72    r
    063   51    33    3                           163   115   73    s
    064   52    34    4                           164   116   74    t
    065   53    35    5                           165   117   75    u
    066   54    36    6                           166   118   76    v
    067   55    37    7                           167   119   77    w
    070   56    38    8                           170   120   78    x
    071   57    39    9                           171   121   79    y
    072   58    3A    :                           172   122   7A    z
    073   59    3B    ;                           173   123   7B    {
    074   60    3C    <                           174   124   7C    |
    075   61    3D    =                           175   125   7D    }
    076   62    3E    >                           176   126   7E    ~
    077   63    3F    ?                           177   127   7F    DEL
*/

#define _ASCII_H
#endif

