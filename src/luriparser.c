/*
 *  Copyright (C) 2013 Masatoshi Teruya
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 *  luriparser.c
 *  Created by Masatoshi Teruya on 13/05/17.
 */

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <lauxlib.h>
#include <lualib.h>
#include <uriparser/Uri.h>


#define lstate_str2tbl(L,k,v) do{ \
    lua_pushstring(L,k); \
    lua_pushstring(L,v); \
    lua_rawset(L,-3); \
}while(0)


#define lstate_strn2tbl(L,k,v,n) do{ \
    lua_pushstring(L,k); \
    lua_pushlstring(L,v,n); \
    lua_rawset(L,-3); \
}while(0)


#define lstate_bool2tbl(L,k,v) do{ \
    lua_pushstring(L,k); \
    lua_pushboolean(L,v); \
    lua_rawset(L,-3); \
}while(0)


#define lstate_pusherr(L,c) do{ \
    switch(c){ \
        case URI_ERROR_SYNTAX: \
            lua_pushstring(L,"Parsed text violates expected format"); \
        break; \
        case URI_ERROR_NULL: \
            lua_pushstring(L,"One of the params passed was NULL"); \
        break; \
        case URI_ERROR_MALLOC: \
            lua_pushstring(L,"Requested memory could not be allocated"); \
        break; \
        case URI_ERROR_OUTPUT_TOO_LARGE: \
            lua_pushstring(L,"Some output is to large for the receiving buffer"); \
        break; \
        case URI_ERROR_NOT_IMPLEMENTED: \
            lua_pushstring(L,"The called function is not implemented yet"); \
        break; \
        case URI_ERROR_RANGE_INVALID: \
            lua_pushstring(L,"The parameters passed contained invalid ranges"); \
        break; \
        case URI_ERROR_ADDBASE_REL_BASE: \
        case URI_ERROR_REMOVEBASE_REL_BASE: \
        case URI_ERROR_REMOVEBASE_REL_SOURCE: \
            lua_pushstring(L,"Given base is not absolute"); \
        break; \
        default: \
            lua_pushstring(L,"Unknwn error"); \
    } \
}while(0)


static int parse_query( lua_State *L, const char *str, size_t len )
{
    UriQueryListA *qry = NULL;
    int nqry = 0;
    int rc = uriDissectQueryMallocA( &qry, &nqry, str, str + len );

    if( rc == URI_SUCCESS )
    {
        UriQueryListA *ptr = qry;

        lua_newtable( L );
        while( ptr )
        {
            if( ptr->key ){
                lstate_str2tbl( L, ptr->key, ptr->value ? ptr->value : "" );
            }
            ptr = ptr->next;
        }
        uriFreeQueryListA( qry );
    }

    return rc;
}


static int parse_lua( lua_State *L )
{
    size_t len = 0;
    const char *url = luaL_checklstring( L, 1, &len );
    int parseQry = 0;
    UriParserStateA state;
    UriUriA uri;
    int rc = 0;

    // check arguments
    if( !lua_isnoneornil( L, 2 ) ){
        luaL_checktype( L, 2, LUA_TBOOLEAN );
        parseQry = lua_toboolean( L, 2 );
    }

    // parse
    state.uri = &uri;
    if( ( rc = uriParseUriExA( &state, url, url + len ) ) == URI_SUCCESS &&
        ( rc = uriNormalizeSyntaxA( &uri ) ) == URI_SUCCESS )
    {
        // create table
        lua_newtable( L );

        // set scheme
        if( uri.scheme.first ){
            lstate_strn2tbl( L, "scheme", uri.scheme.first,
                             uri.scheme.afterLast - uri.scheme.first );
        }
        // set userInfo
        if( uri.userInfo.first ){
            lstate_strn2tbl( L, "userinfo", uri.userInfo.first,
                             uri.userInfo.afterLast - uri.userInfo.first );
        }
        // set hostText
        if( uri.hostText.first ){
            lstate_strn2tbl( L, "host", uri.hostText.first,
                             uri.hostText.afterLast - uri.hostText.first );
        }
        // set portText
        if( uri.portText.first ){
            lstate_strn2tbl( L, "port", uri.portText.first,
                             uri.portText.afterLast - uri.portText.first );
        }

        // set fragment
        if( uri.fragment.first ){
            lstate_strn2tbl( L, "fragment", uri.fragment.first,
                             uri.fragment.afterLast - uri.fragment.first );
        }

        // set query
        if( uri.query.first )
        {
            // no query parse
            if( !parseQry ){
                lstate_strn2tbl( L, "query", uri.query.first,
                                 uri.query.afterLast - uri.query.first );
            }
            else
            {
                lua_pushstring( L, "query" );
                rc = parse_query( L, uri.query.first,
                                  uri.query.afterLast - uri.query.first );
                if( rc == URI_SUCCESS ){
                    lua_rawset( L, -3 );
                }
                else {
                    lua_settop( L, 0 );
                    // free
                    uriFreeUriMembersA( &uri );
                    goto PARSE_FAILURE;
                }
            }
        }

        // set path
        lua_pushliteral( L, "path" );
        if( uri.pathHead && uri.pathHead->text.first )
        {
            UriPathSegmentA *seg = uri.pathHead;
            int top = lua_gettop( L );

            while( seg && seg->text.first != seg->text.afterLast ){
                lua_pushliteral( L, "/" );
                lua_pushlstring(
                    L, seg->text.first, seg->text.afterLast - seg->text.first
                );
                seg = seg->next;
            }

            // push trailing-slash
            if( uri.pathTail->text.first == uri.pathTail->text.afterLast ){
                lua_pushliteral( L, "/" );
            }

            lua_concat( L, lua_gettop( L ) - top );
            lua_rawset( L, -3 );
        }
        else {
            lua_pushliteral( L, "/" );
            lua_rawset( L, -3 );
        }

        // free
        uriFreeUriMembersA( &uri );

        return 1;
    }

PARSE_FAILURE:
    // got error
    lua_pushnil( L );
    lstate_pusherr( L, rc );

    return 2;
}


static int parse_query_lua( lua_State *L )
{
    size_t len = 0;
    const char *qry = luaL_checklstring( L, 1, &len );
    int rc = parse_query( L, qry, len );

    // success
    if( rc == URI_SUCCESS ){
        return 1;
    }

    // got error
    lua_pushnil( L );
    lstate_pusherr( L, rc );

    return 2;
}


LUALIB_API int luaopen_uriparser( lua_State *L )
{
    struct luaL_Reg funcs[] = {
        { "parse", parse_lua },
        { "parseQuery", parse_query_lua },
        { NULL, NULL }
    };
    struct luaL_Reg *ptr = funcs;

    lua_newtable( L );
    // set functions
    while( ptr->name ){
        lua_pushstring( L, ptr->name );
        lua_pushcfunction( L, ptr->func );
        lua_rawset( L, -3 );
        ptr++;
    }

    return 1;
}

