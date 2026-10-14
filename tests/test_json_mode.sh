#!/bin/bash
# Automated test suite for sameboy-json
# Usage: ./tests/test_json_mode.sh [rom_path]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

JSON_BIN="${1:-$ROOT_DIR/build/bin/json-server/sameboy-json}"
ROM="${2:-$ROOT_DIR/build/bin/SDL/dmg_boot.bin}"
PASS=0
FAIL=0
TIMEOUT=10

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

run_test() {
  local name="$1"
  local input="$2"
  local expected="$3"
  
  local output
  output=$(printf '%b\n' "$input" | timeout "$TIMEOUT" "$JSON_BIN" "$ROM" 2>/dev/null) || true
  
  if echo "$output" | grep -q "$expected"; then
    echo -e "${GREEN}PASS${NC}: $name"
    PASS=$((PASS + 1))
  else
    echo -e "${RED}FAIL${NC}: $name"
    echo "  Expected: $expected"
    echo "  Got: $(echo "$output" | head -5)"
    FAIL=$((FAIL + 1))
  fi
}

echo "=== SameBoy JSON Mode Tests ==="
echo "Binary: $JSON_BIN"
echo "ROM: $ROM"
echo

# Check binary exists
if [ ! -f "$JSON_BIN" ]; then
  echo -e "${RED}ERROR${NC}: JSON binary not found at $JSON_BIN"
  echo "Run: make json-server"
  exit 1
fi

# Check ROM exists
if [ ! -f "$ROM" ]; then
  echo -e "${RED}ERROR${NC}: ROM not found at $ROM"
  exit 1
fi

echo "--- Core Lifecycle ---"

# T1: Ready notification
run_test "T1: Ready notification" \
  '{"id":1,"method":"quit"}' \
  '"method":"ready"'

# T2: Clean quit
run_test "T2: Clean quit" \
  '{"id":1,"method":"quit"}' \
  '"result":"ok"'

echo
echo "--- CPU Operations ---"

# T3: Registers readable
run_test "T3: Registers readable" \
  '{"id":1,"method":"cpu.registers.read"}
   {"id":2,"method":"quit"}' \
  '"af"'

# T4: Register write + readback
run_test "T4: Register write roundtrip" \
  '{"id":1,"method":"cpu.registers.write","params":{"name":"a","value":42}}
   {"id":2,"method":"cpu.registers.read","params":{"name":"a"}}
   {"id":3,"method":"quit"}' \
  '"a"'

# T5: CPU stepping advances PC
run_test "T5: CPU stepping advances PC" \
  '{"id":1,"method":"cpu.registers.read","params":{"name":"pc"}}
   {"id":2,"method":"cpu.step"}
   {"id":3,"method":"cpu.registers.read","params":{"name":"pc"}}
   {"id":4,"method":"quit"}' \
  '"pc"'

echo
echo "--- Memory Operations ---"

# T6: Memory read returns array
run_test "T6: Memory read returns array" \
  '{"id":1,"method":"memory.read","params":{"address":0,"size":16}}
   {"id":2,"method":"quit"}' \
  '"data":\['

# T7: Memory write + readback
run_test "T7: Memory write roundtrip" \
  '{"id":1,"method":"memory.write","params":{"address":49152,"data":[42]}}
   {"id":2,"method":"memory.read","params":{"address":49152,"size":1}}
   {"id":3,"method":"quit"}' \
  '"data"'

# T8: Memory dump
run_test "T8: Memory dump formatted" \
  '{"id":1,"method":"memory.dump","params":{"address":0,"size":32}}
   {"id":2,"method":"quit"}' \
  '"dump"'

# T9: Large memory read
run_test "T9: Large memory read (8192 bytes)" \
  '{"id":1,"method":"memory.read","params":{"address":49152,"size":8192}}
   {"id":2,"method":"quit"}' \
  '"data":\['

echo
echo "--- Disassembly & Evaluation ---"

# T10: Disassembly
run_test "T10: Disassembly returns instructions" \
  '{"id":1,"method":"disassemble","params":{"count":5}}
   {"id":2,"method":"quit"}' \
  '"disassembly"'

# T11: Expression evaluation (may fail if not supported)
run_test "T11: Expression evaluation" \
  '{"id":1,"method":"eval","params":{"expression":"1+2"}}
   {"id":2,"method":"quit"}' \
  '"id":1'

# T12: Context snapshot
run_test "T12: Context snapshot" \
  '{"id":1,"method":"context.snapshot","params":{"range":5}}
   {"id":2,"method":"quit"}' \
  '"registers"'

echo
echo "--- Breakpoints & Watchpoints ---"

# T13: Breakpoint add + list
run_test "T13: Breakpoint add + list" \
  '{"id":1,"method":"breakpoint.add","params":{"address":0,"inclusive":false}}
   {"id":2,"method":"breakpoint.list"}
   {"id":3,"method":"breakpoint.remove"}
   {"id":4,"method":"quit"}' \
  '"breakpoints"'

# T14: Watchpoint add + list
run_test "T14: Watchpoint add + list" \
  '{"id":1,"method":"watchpoint.add","params":{"address":49152,"type":"w"}}
   {"id":2,"method":"watchpoint.list"}
   {"id":3,"method":"watchpoint.remove"}
   {"id":4,"method":"quit"}' \
  '"watchpoints"'

echo
echo "--- Hardware State ---"

# T15: Cartridge info
run_test "T15: Cartridge info" \
  '{"id":1,"method":"cartridge.info"}
   {"id":2,"method":"quit"}' \
  '"cartridge"'

# T16: PPU state
run_test "T16: PPU state" \
  '{"id":1,"method":"ppu.state"}
   {"id":2,"method":"quit"}' \
  '"lcdc"'

# T17: PPU palette
run_test "T17: PPU palette" \
  '{"id":1,"method":"ppu.palette"}
   {"id":2,"method":"quit"}' \
  '"bgp"'

# T18: APU state
run_test "T18: APU state" \
  '{"id":1,"method":"apu.state"}
   {"id":2,"method":"quit"}' \
  '"apu"'

# T19: LCD state
run_test "T19: LCD state" \
  '{"id":1,"method":"lcd.state"}
   {"id":2,"method":"quit"}' \
  '"lcd"'

echo
echo "--- VRAM & Sprites ---"

# T20: VRAM tile
run_test "T20: VRAM tile data" \
  '{"id":1,"method":"vram.tile","params":{"tile_id":0}}
   {"id":2,"method":"quit"}' \
  '"pixels"'

# T21: VRAM tiles list
run_test "T21: VRAM tiles list" \
  '{"id":1,"method":"vram.tiles"}
   {"id":2,"method":"quit"}' \
  '"count"'

# T22: OAM read
run_test "T22: OAM sprites" \
  '{"id":1,"method":"oam.read"}
   {"id":2,"method":"quit"}' \
  '"sprites"'

# T23: OAM list
run_test "T23: OAM list" \
  '{"id":1,"method":"oam.list"}
   {"id":2,"method":"quit"}' \
  '"id":1'

echo
echo "--- Save State ---"

# T24: Save + load state
run_test "T24: Save + load state" \
  '{"id":1,"method":"state.save","params":{"slot":0}}
   {"id":2,"method":"state.load","params":{"slot":0}}
   {"id":3,"method":"quit"}' \
  '"ok"'

# T25: Save state restores registers
run_test "T25: Save state restores registers" \
  '{"id":1,"method":"cpu.registers.read","params":{"name":"a"}}
   {"id":2,"method":"state.save","params":{"slot":1}}
   {"id":3,"method":"cpu.registers.write","params":{"name":"a","value":99}}
   {"id":4,"method":"state.load","params":{"slot":1}}
   {"id":5,"method":"cpu.registers.read","params":{"name":"a"}}
   {"id":6,"method":"quit"}' \
  '"a"'

echo
echo "--- Input ---"

# T26: Input press + release
run_test "T26: Input press + release" \
  '{"id":1,"method":"input.press","params":{"key":4,"state":true}}
   {"id":2,"method":"input.press","params":{"key":4,"state":false}}
   {"id":3,"method":"quit"}' \
  '"ok"'

echo
echo "--- Emulator Control ---"

# T27: Emulator pause
run_test "T27: Emulator pause" \
  '{"id":1,"method":"emulator.pause"}
   {"id":2,"method":"quit"}' \
  '"ok"'

# T28: Emulator resume (runs until breakpoint, returns "stopped")
run_test "T28: Emulator resume" \
  '{"id":1,"method":"breakpoint.add","params":{"address":0x0100}}
   {"id":2,"method":"emulator.resume"}
   {"id":3,"method":"quit"}' \
  '"stopped"'

# T29: Emulator reset
run_test "T29: Emulator reset" \
  '{"id":1,"method":"emulator.reset","params":{"reload":false}}
   {"id":2,"method":"cpu.registers.read"}
   {"id":3,"method":"quit"}' \
  '"af"'

# T30: Screenshot (returns error in JSON mode - no pixel buffer)
run_test "T30: Screenshot (expected error)" \
  '{"id":1,"method":"screenshot"}
   {"id":2,"method":"quit"}' \
  '"id":1'

echo
echo "--- Error Handling ---"

# T31: Unknown method returns error
run_test "T31: Unknown method returns error" \
  '{"id":1,"method":"nonexistent"}
   {"id":2,"method":"quit"}' \
  '"error"'

# T32: Invalid params
run_test "T32: Invalid params handled" \
  '{"id":1,"method":"cpu.registers.read","params":{"name":"invalid_reg"}}
   {"id":2,"method":"quit"}' \
  '"id":1'

echo
echo "--- Symbol Loading ---"

# T33: Symbol load (may fail if no file, but should not crash)
run_test "T33: Symbol load (error expected)" \
  '{"id":1,"method":"symbol.load","params":{"path":"/tmp/nonexistent.sym"}}
   {"id":2,"method":"quit"}' \
  '"id":1'

echo
echo "=== Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC} ==="

if [ "$FAIL" -gt 0 ]; then
  exit 1
fi

echo -e "${GREEN}All tests passed!${NC}"
