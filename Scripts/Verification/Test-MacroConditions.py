"""Run production macro Lua against local build-12340 ChatFrame with Lua 5.1.

No Unreal runtime, account state, server or extracted Blizzard assets are changed.
"""
import argparse
import pathlib
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lua', required=True, type=pathlib.Path)
    parser.add_argument('--chat-frame', required=True, type=pathlib.Path)
    parser.add_argument('--runtime-header', type=pathlib.Path, help='Optional baseline header for regression reproduction')
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    header = (args.runtime_header or root / 'Source/WowUI/Private/LuaApi/WowMacroRuntime.h').read_text(encoding='utf-8')
    runtime = '\n'.join(re.findall(r'R"LUA\((.*?)\)LUA"', header, re.DOTALL))
    if not runtime:
        raise RuntimeError('Production macro runtime was not found')
    stock = args.chat_frame.read_text(encoding='utf-8-sig')
    begin = stock.index('function SecureCmdItemParse(item)')
    end = stock.index('-- Pre-populate the secure command hash table', begin)
    # The only runtime initialization dependency is GetCursorInfo/GetActionInfo;
    # stock command handlers resolve their native APIs when called by the tests.
    code = "assert(_VERSION=='Lua 5.1'); GetCursorInfo=function() end; GetActionInfo=function() end; _IdleWowTextLength=string.len\n"
    code += runtime + '\nlocal SecureCmdList={}\n' + stock[begin:end]
    code += '\nQAMacroCommands=SecureCmdList\n'
    code += (root / 'Scripts/Verification/MacroConditions.lua').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='idlewow-macro-') as directory:
        script = pathlib.Path(directory) / 'macro-conditions.lua'
        script.write_text(code, encoding='utf-8')
        subprocess.run([str(args.lua.resolve()), str(script)], check=True)


if __name__ == '__main__':
    main()
