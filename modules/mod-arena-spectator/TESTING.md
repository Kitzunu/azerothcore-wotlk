# Testing Guide for Arena Spectator Module

## Build Testing

### 1. Configure and Build
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/azeroth-server \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSCRIPTS=static -DMODULES=static
make -j$(nproc)
make install
```

### 2. Verify Module is Compiled
Check the CMake output for module configuration. You should see:
```
* Modules configuration (static):
  |
  +- worldserver
  |   +- mod-arena-spectator
  |
```

## Functional Testing

### Prerequisites
- Two players in an active arena match
- One spectator player
- Arena Spectator addon (optional but recommended)

### Test Cases

#### 1. Basic Spectator Commands
```
.spect spectate <player-in-arena>
# Expected: Teleports to arena, enters spectator mode

.spect watch <other-player-in-arena>
# Expected: Switches view to different player

.spect leave
# Expected: Returns to entry point, exits spectator mode
```

#### 2. Spectator Restrictions
Try spectating while:
- In combat → Should fail with error message
- Mounted → Should fail with error message
- In a group → Should fail with error message  
- In instance/dungeon → Should fail with error message
- Queued for BG/arena → Should fail with error message

Expected: Each restriction properly blocks spectating with clear error message

#### 3. Real-time Updates (with addon)
Once spectating, verify addon receives:
- Player health and power updates
- Cooldown information
- Aura applications and removals
- Pet health updates
- Target changes
- Spell casting notifications

#### 4. Multiple Spectators
- Have 2-3 players spectate the same arena
- All should receive updates simultaneously
- Commands should work independently for each spectator

#### 5. Arena Preparation Phase
- Try spectating during arena countdown (non-GM)
- Expected: Queued until arena starts, then auto-teleported

- Try spectating during countdown as GM
- Expected: Immediate teleport to arena

#### 6. Edge Cases
- Spectate a player, then they die
- Spectate a match that ends while spectating
- Try to spectate a spectator (should fail)
- Log out while spectating (should properly clean up state)

## Regression Testing

### Verify Core Functionality Still Works
- Arena matches function normally without spectators
- Arena rewards are distributed correctly
- MMR calculations work
- Arena queue system operates normally

### Verify Spectator Data Hooks
Check that spectator data is sent at correct times:
- Unit.cpp: Power/health changes (`SetHealth`, `SetPower`, etc.)
- Player.cpp: Pet summoning, target changes, cooldowns
- SpellAuras.cpp: Aura applications/removals
- Spell.cpp: Spell interruptions
- MovementHandler.cpp: Spectator enable on teleport

## Performance Testing

### No Spectators
- Run arena matches without spectators
- Monitor server performance (no overhead expected)

### With Spectators
- Multiple spectators (5-10) watching same arena
- Monitor network traffic and CPU usage
- Verify no significant performance impact

## Compatibility Testing

### Build Configurations
Test with different CMake options:
- `MODULES=static` (default) ✓
- `MODULES=dynamic` (if supported)
- `MODULES=disabled` (should fail - module provides required symbols)

### Platform Testing
- Linux (recommended)
- Windows
- macOS

## Addon Testing

### Version Check
```
.spect version 27
# Expected: No "OUTDATED" message

.spect version 26
# Expected: Receives "OUTDATED" message from server
```

### Data Packet Format
Verify addon receives correctly formatted messages:
- Format: `ASSUN\x09<guid>;<key>=<value>;`
- Examples:
  - `ASSUN\x09<guid>;CHP=15000;` (current health)
  - `ASSUN\x09<guid>;MHP=20000;` (max health)
  - `ASSUN\x09<guid>;ACD=42292,30,120;` (cooldown)

## Expected Results

✅ **Success Criteria:**
- All spectator commands work correctly
- Real-time updates function properly
- No core functionality regressions
- No memory leaks or crashes
- Clean server logs (no errors related to spectator)

❌ **Failure Indicators:**
- Spectator commands not recognized
- No real-time updates received
- Server crashes when spectating
- Core functionality broken
- Memory leaks in spectator code

## Troubleshooting

### Module Not Loading
Check worldserver startup logs for:
```
Loading C++ scripts
...
Loading mod_arena_spectator scripts
```

### Commands Not Working
- Verify module is compiled (check CMake output)
- Check worldserver logs for script loading
- Ensure GM level is sufficient (SEC_PLAYER minimum)

### No Real-time Updates
- Check that arena is STATUS_IN_PROGRESS
- Verify spectator flag is set on player
- Check network traffic for addon messages
- Enable SQL logging to see if spectator data is being generated

## Debug Commands

```sql
-- Check player spectator status
SELECT guid, name, extra_flags FROM characters WHERE name = '<player>';
-- PLAYER_EXTRA_SPECTATOR_ON = 0x80

-- Check for active battlegrounds
SELECT * FROM battleground_template WHERE type BETWEEN 2 AND 11;
```

## Post-Testing

After completing tests:
1. Document any issues found
2. Verify all spectators properly cleaned up
3. Check server logs for errors
4. Monitor server stability over time
