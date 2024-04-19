typedef enum {
    GBRight,
    GBLeft,
    GBUp,
    GBDown,
    GBA,
    GBB,
    GBSelect,
    GBStart,
    GBTurbo,
    GBRewind,
    GBUnderclock,
    GBHotkey1,
    GBHotkey2,
    GBJoypadButtonCount,
    GBButtonCount =  GBUnderclock + 1,
    GBGameBoyButtonCount = GBStart + 1,
} GBButton;

#define GBJoyKitHotkey1 JOYButtonUsageGeneric0 + 0x100
#define GBJoyKitHotkey2 JOYButtonUsageGeneric0 + 0x101

extern NSString const *GBButtonNames[GBJoypadButtonCount];

static inline NSString *n2s(uint64_t number)
{
    return [NSString stringWithFormat:@"%llx", number];
}

static inline NSString *button_to_preference_name(GBButton button, unsigned player)
{
    if (player) {
        return [NSString stringWithFormat:@"GBPlayer%d%@", player + 1, GBButtonNames[button]];
    }
    return [NSString stringWithFormat:@"GB%@", GBButtonNames[button]];
}
