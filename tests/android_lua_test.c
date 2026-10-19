/* Lua 5.1 + Android FORTIFY startup/weak-table regression, with the actual Android dependency sources. */
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int main(void) {
  lua_State *state = luaL_newstate();
  if (!state) return 1;
  luaL_openlibs(state); /* The original Android game aborted here, in the GC weak-mode string search. */
  const char *script =
    "local values = setmetatable({}, {__mode='v'})\n"
    "local keys = setmetatable({}, {__mode='k'})\n"
    "local both = setmetatable({}, {__mode='kv'})\n"
    "local strong = setmetatable({}, {__mode='\\0kv'})\n"
    "do local object = {}; values[1]=object; keys[object]=1; both[object]=object; strong[1]=object end\n"
    "for i=1,20000 do local garbage = { tostring(i), string.rep('x',80) } end\n"
    "collectgarbage('collect')\n"
    "assert(values[1] ~= nil and next(keys) ~= nil and next(both) ~= nil)\n"
    "strong[1]=nil; collectgarbage('collect'); collectgarbage('collect')\n"
    "assert(values[1] == nil and next(keys) == nil and next(both) == nil)\n";
  int result = luaL_dostring(state, script);
  if (result) fprintf(stderr, "Lua Android regression failed: %s\n", lua_tostring(state,-1));
  lua_close(state);
  if (!result) puts("PASS Android Lua startup, collection and weak-mode NUL semantics");
  return result ? 1 : 0;
}
