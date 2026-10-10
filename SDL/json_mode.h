#pragma once

// Initialize JSON mode
void json_mode_init(void);

// Run the JSON command loop
void json_mode_run(const char *rom_path);

// Clean up
void json_mode_cleanup(void);

// Print usage
void json_mode_usage(void);
