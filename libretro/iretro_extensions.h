/* Unofficial extension APIs to the libretro API that are supported by iRetro */

#ifndef IRETRO_EXTENSIONS_H
#define IRETRO_EXTENSIONS_H

#include "libretro.h"

/**
 * Returns the amount of data the implementation requires to serialize persistent state.
 *
 * @note The caller is expected to call iretro_persistent_serialize immidiately after this
 * function. The return value is allowed to change between calls to retro_run.
 *
 * @return The amount of data the implementation requires to serialize the persistent state.
 *
 * @see iretro_persistent_serialize()
 */
RETRO_API size_t iretro_persistent_serialize_size(void);

/**
 * Serializes the persistent state, which may include battery-powered RAM, RTC metadata,
 * memory card data, flash memory, etc.
 *
 * @param data A pointer to where the serialized data should be saved to.
 * @param size The size of the memory.
 *
 * @return If failed, or size is lower than \c iretro_persistent_serialize_size(), it
 * should return false. On success, it will return true.
 *
 * @see iretro_persistent_serialize_size()
 * @see iretro_persistent_unserialize()
 */
RETRO_API bool iretro_persistent_serialize(void *data, size_t len);

/**
 * Unserialize the given persistent data, and load it into the current running state.
 *
 * @note Implementations of this function are required to support loading between the
 * first call to retro_load_game (or retro_load_game_special) and before the first call
 * to retro_run. Calls to this function in other times may fail.
 *
 * @return Returns true if loading the data was successful, false otherwise.
 *
 * @see iretro_persistent_serialize()
 */
RETRO_API bool iretro_persistent_unserialize(const void *data, size_t len);

/**
 * Queries the core for some extended metadata key about it, such as a short description
 * or copyright information.
 *
 * @note The returned string pointer must remain valid until at least the next call to
 * this function.
 *
 * @return Returns a string pointer, or NULL if no such value available for the specified
 * key. All strings must be UTF-8 encoded.
 *
 * @see IRETRO_METADATA_KEY_*
 */
RETRO_API const char *iretro_query_metadata(const char *key);

// Short copyright string (e.g. Copyright © 2001-2026 Author Name)
#define IRETRO_METADATA_KEY_COPYRIGHT "iretro.copyright"
/* For emulator cores, a comma separated list of supported consoles. A console may contain
   a semicolon to separate the manufacturer from the console name.
 
   For example: Nintendo;Game Boy,Nintendo;Game Boy Advance */
#define IRETRO_METADATA_KEY_CONSOLES "iretro.console"

/* A URL string pointing to the core license, in plain-text.
   Should not be used if IRETRO_METADATA_KEY_LICENSE_TEXT is used. */
#define IRETRO_METADATA_KEY_LICENSE_URL "iretro.license-url"

/* A string containing the full license text for the core.
   Should not be used if IRETRO_METADATA_KEY_LICENSE_URL is used. */
#define IRETRO_METADATA_KEY_LICENSE_TEXT "iretro.license-text"

/* A string containing the license name for the core (e.g. "Expat License") */
#define IRETRO_METADATA_KEY_LICENSE_NAME "iretro.license-name"

/* A string containing a short (1-2 lines) description of the core */
#define IRETRO_METADATA_KEY_DESCRIPTION "iretro.description"

#endif
