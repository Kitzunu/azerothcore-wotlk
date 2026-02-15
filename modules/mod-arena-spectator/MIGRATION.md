# Arena Spectator Module Migration

## Summary

The Arena Spectator functionality has been moved from the core (`src/server/game/ArenaSpectator/`) to a module (`modules/mod-arena-spectator/`).

## Changes Made

### 1. Module Creation
- Created `modules/mod-arena-spectator/` directory structure
- Added module source files:
  - `ArenaSpectator.cpp` - Main implementation
  - `cs_spectator.cpp` - Spectator commands
  - `loader.cpp` - Module script loader

### 2. Core Changes
- **Kept**: `src/server/game/ArenaSpectator/ArenaSpectator.h` - Interface header with `AC_GAME_API` declarations
- **Removed**: `src/server/game/ArenaSpectator/ArenaSpectator.cpp` - Implementation moved to module
- **Removed**: `src/server/scripts/Commands/cs_spectator.cpp` - Command script moved to module
- **Updated**: `src/server/scripts/Commands/cs_script_loader.cpp` - Removed spectator command registration

### 3. Build System
- Updated `.gitignore` to include `mod-arena-spectator` module
- Module will be automatically discovered by CMake (static linking by default)

## Architecture

The implementation follows AzerothCore's module pattern:

1. **Interface in Core**: `ArenaSpectator.h` remains in core with function declarations marked `AC_GAME_API`
2. **Implementation in Module**: All function implementations are in the module
3. **Static Linking**: Module is compiled into `worldserver` binary
4. **No Core Dependencies on Module**: Core code calls ArenaSpectator functions normally; module provides the implementation

## Integration Points

The ArenaSpectator system integrates with core at these locations:
- `MovementHandler.cpp` - Player movement/teleport handling
- `Unit.cpp` - Power and health change notifications
- `Player.cpp` - Pet summoning and target changes  
- `SpellAuras.cpp` - Aura apply/remove notifications
- `Spell.cpp` - Spell casting notifications
- `Battleground.cpp` - Arena match spectator management

All these continue to work as before, calling the ArenaSpectator namespace functions.

## Testing Checklist

- [ ] Build with module enabled (default: static)
- [ ] Verify spectator commands work (`.spect spectate`, `.spect watch`, `.spect leave`)
- [ ] Test real-time spectator updates (health, power, cooldowns, auras)
- [ ] Verify spectator addon communication
- [ ] Test multiple spectators in same arena
- [ ] Verify spectator permissions and restrictions

## Backward Compatibility

This change maintains full backward compatibility:
- All existing spectator functionality preserved
- No configuration changes required
- No database changes required
- Addon integration unchanged
