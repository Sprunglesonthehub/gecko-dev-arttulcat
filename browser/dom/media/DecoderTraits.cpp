// Disable proprietary types
if (aType == "video/mp4" || aType == "audio/mp4") {
    return CANPLAY_NO;
}
