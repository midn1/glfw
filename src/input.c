//========================================================================
// GLFW 3.5 - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2002-2006 Marcus Geelnard
// Copyright (c) 2006-2019 Camilla Löwy <elmindreda@glfw.org>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================

#include "internal.h"
#include "mappings.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Internal key state used for sticky keys
#define _GLFW_STICK 3

// Internal constants for gamepad mapping source types
#define _GLFW_JOYSTICK_AXIS     1
#define _GLFW_JOYSTICK_BUTTON   2
#define _GLFW_JOYSTICK_HATBIT   3

#define GLFW_MOD_MASK (GLFW_MOD_SHIFT | \
                       GLFW_MOD_CONTROL | \
                       GLFW_MOD_ALT | \
                       GLFW_MOD_SUPER | \
                       GLFW_MOD_CAPS_LOCK | \
                       GLFW_MOD_NUM_LOCK)

// Initializes the platform joystick API if it has not been already
//
static GLFWbool initJoysticks(void)
{
    if (!_glfw.joysticksInitialized)
    {
        if (!_glfw.platform.initJoysticks())
        {
            _glfw.platform.terminateJoysticks();
            return GLFW_FALSE;
        }
    }

    return _glfw.joysticksInitialized = GLFW_TRUE;
}

// Finds a mapping based on joystick GUID
//
static _GLFWmapping* findMapping(const char* guid)
{
    int i;

    for (i = 0;  i < _glfw.mappingCount;  i++)
    {
        if (strcmp(_glfw.mappings[i].guid, guid) == 0)
            return _glfw.mappings + i;
    }

    return NULL;
}

// Checks whether a gamepad mapping element is present in the hardware
//
static GLFWbool isValidElementForJoystick(const _GLFWmapelement* e,
                                          const _GLFWjoystick* js)
{
    if (e->type == _GLFW_JOYSTICK_HATBIT && (e->index >> 4) >= js->hatCount)
        return GLFW_FALSE;
    else if (e->type == _GLFW_JOYSTICK_BUTTON && e->index >= js->buttonCount)
        return GLFW_FALSE;
    else if (e->type == _GLFW_JOYSTICK_AXIS && e->index >= js->axisCount)
        return GLFW_FALSE;

    return GLFW_TRUE;
}

// Finds a mapping based on joystick GUID and verifies element indices
//
static _GLFWmapping* findValidMapping(const _GLFWjoystick* js)
{
    _GLFWmapping* mapping = findMapping(js->guid);
    if (mapping)
    {
        int i;

        for (i = 0;  i <= GLFW_GAMEPAD_BUTTON_LAST;  i++)
        {
            if (!isValidElementForJoystick(mapping->buttons + i, js))
                return NULL;
        }

        for (i = 0;  i <= GLFW_GAMEPAD_AXIS_LAST;  i++)
        {
            if (!isValidElementForJoystick(mapping->axes + i, js))
                return NULL;
        }
    }

    return mapping;
}

// Parses an SDL_GameControllerDB line and adds it to the mapping list
//
static GLFWbool parseMapping(_GLFWmapping* mapping, const char* string)
{
    const char* c = string;
    size_t i, length;
    struct
    {
        const char* name;
        _GLFWmapelement* element;
    } fields[] =
    {
        { "platform",      NULL },
        { "a",             mapping->buttons + GLFW_GAMEPAD_BUTTON_A },
        { "b",             mapping->buttons + GLFW_GAMEPAD_BUTTON_B },
        { "x",             mapping->buttons + GLFW_GAMEPAD_BUTTON_X },
        { "y",             mapping->buttons + GLFW_GAMEPAD_BUTTON_Y },
        { "back",          mapping->buttons + GLFW_GAMEPAD_BUTTON_BACK },
        { "start",         mapping->buttons + GLFW_GAMEPAD_BUTTON_START },
        { "guide",         mapping->buttons + GLFW_GAMEPAD_BUTTON_GUIDE },
        { "leftshoulder",  mapping->buttons + GLFW_GAMEPAD_BUTTON_LEFT_BUMPER },
        { "rightshoulder", mapping->buttons + GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER },
        { "leftstick",     mapping->buttons + GLFW_GAMEPAD_BUTTON_LEFT_THUMB },
        { "rightstick",    mapping->buttons + GLFW_GAMEPAD_BUTTON_RIGHT_THUMB },
        { "dpup",          mapping->buttons + GLFW_GAMEPAD_BUTTON_DPAD_UP },
        { "dpright",       mapping->buttons + GLFW_GAMEPAD_BUTTON_DPAD_RIGHT },
        { "dpdown",        mapping->buttons + GLFW_GAMEPAD_BUTTON_DPAD_DOWN },
        { "dpleft",        mapping->buttons + GLFW_GAMEPAD_BUTTON_DPAD_LEFT },
        { "lefttrigger",   mapping->axes + GLFW_GAMEPAD_AXIS_LEFT_TRIGGER },
        { "righttrigger",  mapping->axes + GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER },
        { "leftx",         mapping->axes + GLFW_GAMEPAD_AXIS_LEFT_X },
        { "lefty",         mapping->axes + GLFW_GAMEPAD_AXIS_LEFT_Y },
        { "rightx",        mapping->axes + GLFW_GAMEPAD_AXIS_RIGHT_X },
        { "righty",        mapping->axes + GLFW_GAMEPAD_AXIS_RIGHT_Y }
    };

    length = strcspn(c, ",");
    if (length != 32 || c[length] != ',')
    {
        _glfwInputError(GLFW_INVALID_VALUE, NULL);
        return GLFW_FALSE;
    }

    memcpy(mapping->guid, c, length);
    c += length + 1;

    length = strcspn(c, ",");
    if (length >= sizeof(mapping->name) || c[length] != ',')
    {
        _glfwInputError(GLFW_INVALID_VALUE, NULL);
        return GLFW_FALSE;
    }

    memcpy(mapping->name, c, length);
    c += length + 1;

    while (*c)
    {
        // TODO: Implement output modifiers
        if (*c == '+' || *c == '-')
            return GLFW_FALSE;

        for (i = 0;  i < sizeof(fields) / sizeof(fields[0]);  i++)
        {
            length = strlen(fields[i].name);
            if (strncmp(c, fields[i].name, length) != 0 || c[length] != ':')
                continue;

            c += length + 1;

            if (fields[i].element)
            {
                _GLFWmapelement* e = fields[i].element;
                int8_t minimum = -1;
                int8_t maximum = 1;

                if (*c == '+')
                {
                    minimum = 0;
                    c += 1;
                }
                else if (*c == '-')
                {
                    maximum = 0;
                    c += 1;
                }

                if (*c == 'a')
                    e->type = _GLFW_JOYSTICK_AXIS;
                else if (*c == 'b')
                    e->type = _GLFW_JOYSTICK_BUTTON;
                else if (*c == 'h')
                    e->type = _GLFW_JOYSTICK_HATBIT;
                else
                    break;

                if (e->type == _GLFW_JOYSTICK_HATBIT)
                {
                    const unsigned long hat = strtoul(c + 1, (char**) &c, 10);
                    const unsigned long bit = strtoul(c + 1, (char**) &c, 10);
                    e->index = (uint8_t) ((hat << 4) | bit);
                }
                else
                    e->index = (uint8_t) strtoul(c + 1, (char**) &c, 10);

                if (e->type == _GLFW_JOYSTICK_AXIS)
                {
                    e->axisScale = 2 / (maximum - minimum);
                    e->axisOffset = -(maximum + minimum);

                    if (*c == '~')
                    {
                        e->axisScale = -e->axisScale;
                        e->axisOffset = -e->axisOffset;
                    }
                }
            }
            else
            {
                const char* name = _glfw.platform.getMappingName();
                length = strlen(name);
                if (strncmp(c, name, length) != 0)
                    return GLFW_FALSE;
            }

            break;
        }

        c += strcspn(c, ",");
        c += strspn(c, ",");
    }

    for (i = 0;  i < 32;  i++)
    {
        if (mapping->guid[i] >= 'A' && mapping->guid[i] <= 'F')
            mapping->guid[i] += 'a' - 'A';
    }

    _glfw.platform.updateGamepadGUID(mapping->guid);
    return GLFW_TRUE;
}


//////////////////////////////////////////////////////////////////////////
//////                         GLFW event API                       //////
//////////////////////////////////////////////////////////////////////////

// Notifies shared code of a physical key event
//
void _glfwInputKey(_GLFWwindow* window, int key, int scancode, int action, int mods)
{
    assert(window != NULL);
    assert(key >= 0 || key == GLFW_KEY_UNKNOWN);
    assert(key <= GLFW_KEY_LAST);
    assert(action == GLFW_PRESS || action == GLFW_RELEASE);
    assert(mods == (mods & GLFW_MOD_MASK));

    if (key >= 0 && key <= GLFW_KEY_LAST)
    {
        GLFWbool repeated = GLFW_FALSE;

        if (action == GLFW_RELEASE && window->keys[key] == GLFW_RELEASE)
            return;

        if (action == GLFW_PRESS && window->keys[key] == GLFW_PRESS)
            repeated = GLFW_TRUE;

        if (action == GLFW_RELEASE && window->stickyKeys)
            window->keys[key] = _GLFW_STICK;
        else
            window->keys[key] = (char) action;

        if (repeated)
            action = GLFW_REPEAT;
    }

    if (!window->lockKeyMods)
        mods &= ~(GLFW_MOD_CAPS_LOCK | GLFW_MOD_NUM_LOCK);

    if (window->callbacks.key)
        window->callbacks.key((GLFWwindow*) window, key, scancode, action, mods);
}

// Notifies shared code of a Unicode codepoint input event
// The 'plain' parameter determines whether to emit a regular character event
//
void _glfwInputChar(_GLFWwindow* window, uint32_t codepoint, int mods, GLFWbool plain)
{
    assert(window != NULL);
    assert(mods == (mods & GLFW_MOD_MASK));
    assert(plain == GLFW_TRUE || plain == GLFW_FALSE);

    if (codepoint < 32 || (codepoint > 126 && codepoint < 160))
        return;

    if (!window->lockKeyMods)
        mods &= ~(GLFW_MOD_CAPS_LOCK | GLFW_MOD_NUM_LOCK);

    if (window->callbacks.charmods)
        window->callbacks.charmods((GLFWwindow*) window, codepoint, mods);

    if (plain)
    {
        if (window->callbacks.character)
            window->callbacks.character((GLFWwindow*) window, codepoint);
    }
}

// Notifies shared code of a scroll event
//
void _glfwInputScroll(_GLFWwindow* window, double xoffset, double yoffset)
{
    assert(window != NULL);
    assert(xoffset > -FLT_MAX);
    assert(xoffset < FLT_MAX);
    assert(yoffset > -FLT_MAX);
    assert(yoffset < FLT_MAX);

    if (window->callbacks.scroll)
        window->callbacks.scroll((GLFWwindow*) window, xoffset, yoffset);
}

// Notifies shared code of a mouse button click event
//
void _glfwInputMouseClick(_GLFWwindow* window, int button, int action, int mods)
{
    assert(window != NULL);
    assert(button >= 0);
    assert(action == GLFW_PRESS || action == GLFW_RELEASE);
    assert(mods == (mods & GLFW_MOD_MASK));

    if (button < 0 || (!window->disableMouseButtonLimit && button > GLFW_MOUSE_BUTTON_LAST))
        return;

    if (!window->lockKeyMods)
        mods &= ~(GLFW_MOD_CAPS_LOCK | GLFW_MOD_NUM_LOCK);

    if (button <= GLFW_MOUSE_BUTTON_LAST)
    {
        if (action == GLFW_RELEASE && window->stickyMouseButtons)
            window->mouseButtons[button] = _GLFW_STICK;
        else
            window->mouseButtons[button] = (char) action;
    }

    if (window->callbacks.mouseButton)
        window->callbacks.mouseButton((GLFWwindow*) window, button, action, mods);
}

// Notifies shared code of a cursor motion event
// The position is specified in content area relative screen coordinates
//
void _glfwInputCursorPos(_GLFWwindow* window, double xpos, double ypos)
{
    assert(window != NULL);
    assert(xpos > -FLT_MAX);
    assert(xpos < FLT_MAX);
    assert(ypos > -FLT_MAX);
    assert(ypos < FLT_MAX);

    if (window->virtualCursorPosX == xpos && window->virtualCursorPosY == ypos)
        return;

    window->virtualCursorPosX = xpos;
    window->virtualCursorPosY = ypos;

    if (window->callbacks.cursorPos)
        window->callbacks.cursorPos((GLFWwindow*) window, xpos, ypos);
}

// Notifies shared code of a cursor enter/leave event
//
void _glfwInputCursorEnter(_GLFWwindow* window, GLFWbool entered)
{
    assert(window != NULL);
    assert(entered == GLFW_TRUE || entered == GLFW_FALSE);

    if (window->callbacks.cursorEnter)
        window->callbacks.cursorEnter((GLFWwindow*) window, entered);
}

// Notifies shared code of files or directories dropped on a window
//
void _glfwInputDrop(_GLFWwindow* window, int count, const char** paths)
{
    assert(window != NULL);
    assert(count > 0);
    assert(paths != NULL);

    if (window->callbacks.drop)
        window->callbacks.drop((GLFWwindow*) window, count, paths);
}

// Notifies shared code of a joystick connection or disconnection
//
void _glfwInputJoystick(_GLFWjoystick* js, int event)
{
    assert(js != NULL);
    assert(event == GLFW_CONNECTED || event == GLFW_DISCONNECTED);

    if (event == GLFW_CONNECTED)
        js->connected = GLFW_TRUE;
    else if (event == GLFW_DISCONNECTED)
        js->connected = GLFW_FALSE;

    if (_glfw.callbacks.joystick)
        _glfw.callbacks.joystick((int) (js - _glfw.joysticks), event);
}

// Notifies shared code of the new value of a joystick axis
//
void _glfwInputJoystickAxis(_GLFWjoystick* js, int axis, float value)
{
    assert(js != NULL);
    assert(axis >= 0);
    assert(axis < js->axisCount);

    js->axes[axis] = value;
}

// Notifies shared code of the new value of a joystick button
//
void _glfwInputJoystickButton(_GLFWjoystick* js, int button, char value)
{
    assert(js != NULL);
    assert(button >= 0);
    assert(button < js->buttonCount);
    assert(value == GLFW_PRESS || value == GLFW_RELEASE);

    js->buttons[button] = value;
}

// Notifies shared code of the new value of a joystick hat
//
void _glfwInputJoystickHat(_GLFWjoystick* js, int hat, char value)
{
    int base;

    assert(js != NULL);
    assert(hat >= 0);
    assert(hat < js->hatCount);

    // Valid hat values only use the least significant nibble
    assert((value & 0xf0) == 0);
    // Valid hat values do not have both bits of an axis set
    assert((value & GLFW_HAT_LEFT) == 0 || (value & GLFW_HAT_RIGHT) == 0);
    assert((value & GLFW_HAT_UP) == 0 || (value & GLFW_HAT_DOWN) == 0);

    base = js->buttonCount + hat * 4;

    js->buttons[base + 0] = (value & 0x01) ? GLFW_PRESS : GLFW_RELEASE;
    js->buttons[base + 1] = (value & 0x02) ? GLFW_PRESS : GLFW_RELEASE;
    js->buttons[base + 2] = (value & 0x04) ? GLFW_PRESS : GLFW_RELEASE;
    js->buttons[base + 3] = (value & 0x08) ? GLFW_PRESS : GLFW_RELEASE;

    js->hats[hat] = value;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

// Adds the built-in set of gamepad mappings
//
void _glfwInitGamepadMappings(void)
{
    size_t i;
    const size_t count = sizeof(_glfwDefaultMappings) / sizeof(char*);
    _glfw.mappings = _glfw_calloc(count, sizeof(_GLFWmapping));

    for (i = 0;  i < count;  i++)
    {
        if (parseMapping(&_glfw.mappings[_glfw.mappingCount], _glfwDefaultMappings[i]))
            _glfw.mappingCount++;
    }
}

// Returns an available joystick object with arrays and name allocated
//
_GLFWjoystick* _glfwAllocJoystick(const char* name,
                                  const char* guid,
                                  int axisCount,
                                  int buttonCount,
                                  int hatCount)
{
    int jid;
    _GLFWjoystick* js;

    for (jid = 0;  jid <= GLFW_JOYSTICK_LAST;  jid++)
    {
        if (!_glfw.joysticks[jid].allocated)
            break;
    }

    if (jid > GLFW_JOYSTICK_LAST)
        return NULL;

    js = _glfw.joysticks + jid;
    js->allocated   = GLFW_TRUE;
    js->axes        = _glfw_calloc(axisCount, sizeof(float));
    js->buttons     = _glfw_calloc(buttonCount + (size_t) hatCount * 4, 1);
    js->hats        = _glfw_calloc(hatCount, 1);
    js->axisCount   = axisCount;
    js->buttonCount = buttonCount;
    js->hatCount    = hatCount;

    strncpy(js->name, name, sizeof(js->name) - 1);
    strncpy(js->guid, guid, sizeof(js->guid) - 1);
    js->mapping = findValidMapping(js);

    return js;
}

// Frees arrays and name and flags the joystick object as unused
//
void _glfwFreeJoystick(_GLFWjoystick* js)
{
    _glfw_free(js->axes);
    _glfw_free(js->buttons);
    _glfw_free(js->hats);
    memset(js, 0, sizeof(_GLFWjoystick));
}

// Center the cursor in the content area of the specified window
//
void _glfwCenterCursorInContentArea(_GLFWwindow* window)
{
    int width, height;

    _glfw.platform.getWindowSize(window, &width, &height);
    _glfw.platform.setCursorPos(window, width / 2.0, height / 2.0);
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW public API                       //////
//////////////////////////////////////////////////////////////////////////

GLFWAPI int glfwGetInputMode(GLFWwindow* handle, int mode)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    switch (mode)
    {
        case GLFW_CURSOR:
            return window->cursorMode;
        case GLFW_STICKY_KEYS:
            return window->stickyKeys;
        case GLFW_STICKY_MOUSE_BUTTONS:
            return window->stickyMouseButtons;
        case GLFW_LOCK_KEY_MODS:
            return window->lockKeyMods;
        case GLFW_RAW_MOUSE_MOTION:
            return window->rawMouseMotion;
        case GLFW_UNLIMITED_MOUSE_BUTTONS:
            return window->disableMouseButtonLimit;
    }

    _glfwInputError(GLFW_INVALID_ENUM, "Invalid input mode 0x%08X", mode);
    return 0;
}

GLFWAPI void glfwSetInputMode(GLFWwindow* handle, int mode, int value)
{
    _GLFW_REQUIRE_INIT();

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    switch (mode)
    {
        case GLFW_CURSOR:
        {
            if (value != GLFW_CURSOR_NORMAL &&
                value != GLFW_CURSOR_HIDDEN &&
                value != GLFW_CURSOR_DISABLED &&
                value != GLFW_CURSOR_CAPTURED)
            {
                _glfwInputError(GLFW_INVALID_ENUM,
                                "Invalid cursor mode 0x%08X",
                                value);
                return;
            }

            if (window->cursorMode == value)
                return;

            window->cursorMode = value;

            _glfw.platform.getCursorPos(window,
                                        &window->virtualCursorPosX,
                                        &window->virtualCursorPosY);
            _glfw.platform.setCursorMode(window, value);
            return;
        }

        case GLFW_STICKY_KEYS:
        {
            value = value ? GLFW_TRUE : GLFW_FALSE;
            if (window->stickyKeys == value)
                return;

            if (!value)
            {
                int i;

                // Release all sticky keys
                for (i = 0;  i <= GLFW_KEY_LAST;  i++)
                {
                    if (window->keys[i] == _GLFW_STICK)
                        window->keys[i] = GLFW_RELEASE;
                }
            }

            window->stickyKeys = value;
            return;
        }

        case GLFW_STICKY_MOUSE_BUTTONS:
        {
            value = value ? GLFW_TRUE : GLFW_FALSE;
            if (window->stickyMouseButtons == value)
                return;

            if (!value)
            {
                int i;

                // Release all sticky mouse buttons
                for (i = 0;  i <= GLFW_MOUSE_BUTTON_LAST;  i++)
                {
                    if (window->mouseButtons[i] == _GLFW_STICK)
                        window->mouseButtons[i] = GLFW_RELEASE;
                }
            }

            window->stickyMouseButtons = value;
            return;
        }

        case GLFW_LOCK_KEY_MODS:
        {
            window->lockKeyMods = value ? GLFW_TRUE : GLFW_FALSE;
            return;
        }

        case GLFW_RAW_MOUSE_MOTION:
        {
            if (!_glfw.platform.rawMouseMotionSupported())
            {
                _glfwInputError(GLFW_PLATFORM_ERROR,
                                "Raw mouse motion is not supported on this system");
                return;
            }

            value = value ? GLFW_TRUE : GLFW_FALSE;
            if (window->rawMouseMotion == value)
                return;

            window->rawMouseMotion = value;
            _glfw.platform.setRawMouseMotion(window, value);
            return;
        }

        case GLFW_UNLIMITED_MOUSE_BUTTONS:
        {
            window->disableMouseButtonLimit = value ? GLFW_TRUE : GLFW_FALSE;
            return;
        }
    }

    _glfwInputError(GLFW_INVALID_ENUM, "Invalid input mode 0x%08X", mode);
}

GLFWAPI int glfwRawMouseMotionSupported(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_FALSE);
    return _glfw.platform.rawMouseMotionSupported();
}

GLFWAPI const char* glfwGetKeyName(int key, int scancode)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (key != GLFW_KEY_UNKNOWN)
    {
        if (key < GLFW_KEY_SPACE || key > GLFW_KEY_LAST)
        {
            _glfwInputError(GLFW_INVALID_ENUM, "Invalid key %i", key);
            return NULL;
        }

        if (key != GLFW_KEY_KP_EQUAL &&
            (key < GLFW_KEY_KP_0 || key > GLFW_KEY_KP_ADD) &&
            (key < GLFW_KEY_APOSTROPHE || key > GLFW_KEY_WORLD_2))
        {
            return NULL;
        }

        scancode = _glfw.platform.getKeyScancode(key);
    }

    return _glfw.platform.getScancodeName(scancode);
}

GLFWAPI int glfwGetKeyScancode(int key)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);

    if (key < GLFW_KEY_SPACE || key > GLFW_KEY_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid key %i", key);
        return -1;
    }

    return _glfw.platform.getKeyScancode(key);
}

GLFWAPI int glfwGetKey(GLFWwindow* handle, int key)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_RELEASE);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    if (key < GLFW_KEY_SPACE || key > GLFW_KEY_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid key %i", key);
        return GLFW_RELEASE;
    }

    if (window->keys[key] == _GLFW_STICK)
    {
        // Sticky mode: release key now
        window->keys[key] = GLFW_RELEASE;
        return GLFW_PRESS;
    }

    return (int) window->keys[key];
}

GLFWAPI int glfwGetMouseButton(GLFWwindow* handle, int button)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_RELEASE);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    if (button < GLFW_MOUSE_BUTTON_1 || button > GLFW_MOUSE_BUTTON_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid mouse button %i", button);
        return GLFW_RELEASE;
    }

    if (window->mouseButtons[button] == _GLFW_STICK)
    {
        // Sticky mode: release mouse button now
        window->mouseButtons[button] = GLFW_RELEASE;
        return GLFW_PRESS;
    }

    return (int) window->mouseButtons[button];
}

GLFWAPI void glfwGetCursorPos(GLFWwindow* handle, double* xpos, double* ypos)
{
    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;

    _GLFW_REQUIRE_INIT();

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    if (window->cursorMode == GLFW_CURSOR_DISABLED)
    {
        if (xpos)
            *xpos = window->virtualCursorPosX;
        if (ypos)
            *ypos = window->virtualCursorPosY;
    }
    else
        _glfw.platform.getCursorPos(window, xpos, ypos);
}

GLFWAPI void glfwSetCursorPos(GLFWwindow* handle, double xpos, double ypos)
{
    _GLFW_REQUIRE_INIT();

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    if (xpos != xpos || xpos < -DBL_MAX || xpos > DBL_MAX ||
        ypos != ypos || ypos < -DBL_MAX || ypos > DBL_MAX)
    {
        _glfwInputError(GLFW_INVALID_VALUE,
                        "Invalid cursor position %f %f",
                        xpos, ypos);
        return;
    }

    if (!_glfw.platform.windowFocused(window))
        return;

    if (window->cursorMode == GLFW_CURSOR_DISABLED)
    {
        // Only update the accumulated position if the cursor is disabled
        window->virtualCursorPosX = xpos;
        window->virtualCursorPosY = ypos;
    }
    else
    {
        // Update system cursor position
        _glfw.platform.setCursorPos(window, xpos, ypos);
    }
}

GLFWAPI GLFWcursor* glfwCreateCursor(const GLFWimage* image, int xhot, int yhot)
{
    _GLFWcursor* cursor;

    assert(image != NULL);
    assert(image->pixels != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (image->width <= 0 || image->height <= 0)
    {
        _glfwInputError(GLFW_INVALID_VALUE, "Invalid image dimensions for cursor");
        return NULL;
    }

    cursor = _glfw_calloc(1, sizeof(_GLFWcursor));
    cursor->next = _glfw.cursorListHead;
    _glfw.cursorListHead = cursor;

    if (!_glfw.platform.createCursor(cursor, image, xhot, yhot))
    {
        glfwDestroyCursor((GLFWcursor*) cursor);
        return NULL;
    }

    return (GLFWcursor*) cursor;
}

GLFWAPI GLFWcursor* glfwCreateStandardCursor(int shape)
{
    _GLFWcursor* cursor;

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (shape != GLFW_ARROW_CURSOR &&
        shape != GLFW_IBEAM_CURSOR &&
        shape != GLFW_CROSSHAIR_CURSOR &&
        shape != GLFW_POINTING_HAND_CURSOR &&
        shape != GLFW_RESIZE_EW_CURSOR &&
        shape != GLFW_RESIZE_NS_CURSOR &&
        shape != GLFW_RESIZE_NWSE_CURSOR &&
        shape != GLFW_RESIZE_NESW_CURSOR &&
        shape != GLFW_RESIZE_ALL_CURSOR &&
        shape != GLFW_NOT_ALLOWED_CURSOR)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid standard cursor 0x%08X", shape);
        return NULL;
    }

    cursor = _glfw_calloc(1, sizeof(_GLFWcursor));
    cursor->next = _glfw.cursorListHead;
    _glfw.cursorListHead = cursor;

    if (!_glfw.platform.createStandardCursor(cursor, shape))
    {
        glfwDestroyCursor((GLFWcursor*) cursor);
        return NULL;
    }

    return (GLFWcursor*) cursor;
}

GLFWAPI void glfwDestroyCursor(GLFWcursor* handle)
{
    _GLFW_REQUIRE_INIT();

    _GLFWcursor* cursor = (_GLFWcursor*) handle;

    if (cursor == NULL)
        return;

    // Make sure the cursor is not being used by any window
    {
        _GLFWwindow* window;

        for (window = _glfw.windowListHead;  window;  window = window->next)
        {
            if (window->cursor == cursor)
                glfwSetCursor((GLFWwindow*) window, NULL);
        }
    }

    _glfw.platform.destroyCursor(cursor);

    // Unlink cursor from global linked list
    {
        _GLFWcursor** prev = &_glfw.cursorListHead;

        while (*prev != cursor)
            prev = &((*prev)->next);

        *prev = cursor->next;
    }

    _glfw_free(cursor);
}

GLFWAPI void glfwSetCursor(GLFWwindow* windowHandle, GLFWcursor* cursorHandle)
{
    _GLFW_REQUIRE_INIT();

    _GLFWwindow* window = (_GLFWwindow*) windowHandle;
    _GLFWcursor* cursor = (_GLFWcursor*) cursorHandle;
    assert(window != NULL);

    window->cursor = cursor;

    _glfw.platform.setCursor(window, cursor);
}

GLFWAPI GLFWkeyfun glfwSetKeyCallback(GLFWwindow* handle, GLFWkeyfun cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_SWAP(GLFWkeyfun, window->callbacks.key, cbfun);
    return cbfun;
}

GLFWAPI GLFWcharfun glfwSetCharCallback(GLFWwindow* handle, GLFWcharfun cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_SWAP(GLFWcharfun, window->callbacks.character, cbfun);
    return cbfun;
}

GLFWAPI GLFWcharmodsfun glfwSetCharModsCallback(GLFWwindow* handle, GLFWcharmodsfun cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_SWAP(GLFWcharmodsfun, window->callbacks.charmods, cbfun);
    return cbfun;
}

GLFWAPI GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* handle,
                                                      GLFWmousebuttonfun cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_SWAP(GLFWmousebuttonfun, window->callbacks.mouseButton, cbfun);
    return cbfun;
}

GLFWAPI GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* handle,
                                                  GLFWcursorposfun cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_SWAP(GLFWcursorposfun, window->callbacks.cursorPos, cbfun);
    return cbfun;
}

GLFWAPI GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow* handle,
                                                      GLFWcursorenterfun cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_SWAP(GLFWcursorenterfun, window->callbacks.cursorEnter, cbfun);
    return cbfun;
}

GLFWAPI GLFWscrollfun glfwSetScrollCallback(GLFWwindow* handle,
                                            GLFWscrollfun cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_SWAP(GLFWscrollfun, window->callbacks.scroll, cbfun);
    return cbfun;
}

GLFWAPI GLFWdropfun glfwSetDropCallback(GLFWwindow* handle, GLFWdropfun cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_SWAP(GLFWdropfun, window->callbacks.drop, cbfun);
    return cbfun;
}

GLFWAPI int glfwJoystickPresent(int jid)
{
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);

    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_FALSE);

    if (jid < 0 || jid > GLFW_JOYSTICK_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid joystick ID %i", jid);
        return GLFW_FALSE;
    }

    if (!initJoysticks())
        return GLFW_FALSE;

    js = _glfw.joysticks + jid;
    if (!js->connected)
        return GLFW_FALSE;

    return _glfw.platform.pollJoystick(js, _GLFW_POLL_PRESENCE);
}

GLFWAPI const float* glfwGetJoystickAxes(int jid, int* count)
{
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);
    assert(count != NULL);

    *count = 0;

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (jid < 0 || jid > GLFW_JOYSTICK_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid joystick ID %i", jid);
        return NULL;
    }

    if (!initJoysticks())
        return NULL;

    js = _glfw.joysticks + jid;
    if (!js->connected)
        return NULL;

    if (!_glfw.platform.pollJoystick(js, _GLFW_POLL_AXES))
        return NULL;

    *count = js->axisCount;
    return js->axes;
}

GLFWAPI const unsigned char* glfwGetJoystickButtons(int jid, int* count)
{
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);
    assert(count != NULL);

    *count = 0;

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (jid < 0 || jid > GLFW_JOYSTICK_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid joystick ID %i", jid);
        return NULL;
    }

    if (!initJoysticks())
        return NULL;

    js = _glfw.joysticks + jid;
    if (!js->connected)
        return NULL;

    if (!_glfw.platform.pollJoystick(js, _GLFW_POLL_BUTTONS))
        return NULL;

    if (_glfw.hints.init.hatButtons)
        *count = js->buttonCount + js->hatCount * 4;
    else
        *count = js->buttonCount;

    return js->buttons;
}

GLFWAPI const unsigned char* glfwGetJoystickHats(int jid, int* count)
{
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);
    assert(count != NULL);

    *count = 0;

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (jid < 0 || jid > GLFW_JOYSTICK_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid joystick ID %i", jid);
        return NULL;
    }

    if (!initJoysticks())
        return NULL;

    js = _glfw.joysticks + jid;
    if (!js->connected)
        return NULL;

    if (!_glfw.platform.pollJoystick(js, _GLFW_POLL_BUTTONS))
        return NULL;

    *count = js->hatCount;
    return js->hats;
}

GLFWAPI const char* glfwGetJoystickName(int jid)
{
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (jid < 0 || jid > GLFW_JOYSTICK_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid joystick ID %i", jid);
        return NULL;
    }

    if (!initJoysticks())
        return NULL;

    js = _glfw.joysticks + jid;
    if (!js->connected)
        return NULL;

    if (!_glfw.platform.pollJoystick(js, _GLFW_POLL_PRESENCE))
        return NULL;

    return js->name;
}

GLFWAPI const char* glfwGetJoystickGUID(int jid)
{
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (jid < 0 || jid > GLFW_JOYSTICK_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid joystick ID %i", jid);
        return NULL;
    }

    if (!initJoysticks())
        return NULL;

    js = _glfw.joysticks + jid;
    if (!js->connected)
        return NULL;

    if (!_glfw.platform.pollJoystick(js, _GLFW_POLL_PRESENCE))
        return NULL;

    return js->guid;
}

GLFWAPI void glfwSetJoystickUserPointer(int jid, void* pointer)
{
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);

    _GLFW_REQUIRE_INIT();

    js = _glfw.joysticks + jid;
    if (!js->allocated)
        return;

    js->userPointer = pointer;
}

GLFWAPI void* glfwGetJoystickUserPointer(int jid)
{
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    js = _glfw.joysticks + jid;
    if (!js->allocated)
        return NULL;

    return js->userPointer;
}

GLFWAPI GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun cbfun)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (!initJoysticks())
        return NULL;

    _GLFW_SWAP(GLFWjoystickfun, _glfw.callbacks.joystick, cbfun);
    return cbfun;
}

GLFWAPI int glfwUpdateGamepadMappings(const char* string)
{
    int jid;
    const char* c = string;

    assert(string != NULL);

    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_FALSE);

    while (*c)
    {
        if ((*c >= '0' && *c <= '9') ||
            (*c >= 'a' && *c <= 'f') ||
            (*c >= 'A' && *c <= 'F'))
        {
            char line[1024];

            const size_t length = strcspn(c, "\r\n");
            if (length < sizeof(line))
            {
                _GLFWmapping mapping = {{0}};

                memcpy(line, c, length);
                line[length] = '\0';

                if (parseMapping(&mapping, line))
                {
                    _GLFWmapping* previous = findMapping(mapping.guid);
                    if (previous)
                        *previous = mapping;
                    else
                    {
                        _glfw.mappingCount++;
                        _glfw.mappings =
                            _glfw_realloc(_glfw.mappings,
                                          sizeof(_GLFWmapping) * _glfw.mappingCount);
                        _glfw.mappings[_glfw.mappingCount - 1] = mapping;
                    }
                }
            }

            c += length;
        }
        else
        {
            c += strcspn(c, "\r\n");
            c += strspn(c, "\r\n");
        }
    }

    for (jid = 0;  jid <= GLFW_JOYSTICK_LAST;  jid++)
    {
        _GLFWjoystick* js = _glfw.joysticks + jid;
        if (js->connected)
            js->mapping = findValidMapping(js);
    }

    return GLFW_TRUE;
}

GLFWAPI int glfwJoystickIsGamepad(int jid)
{
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);

    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_FALSE);

    if (jid < 0 || jid > GLFW_JOYSTICK_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid joystick ID %i", jid);
        return GLFW_FALSE;
    }

    if (!initJoysticks())
        return GLFW_FALSE;

    js = _glfw.joysticks + jid;
    if (!js->connected)
        return GLFW_FALSE;

    if (!_glfw.platform.pollJoystick(js, _GLFW_POLL_PRESENCE))
        return GLFW_FALSE;

    return js->mapping != NULL;
}

GLFWAPI GLFWmotion* glfwJoystickGetMotion(int jid)
{
    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);
    
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);
    
    if (!initJoysticks())
        return NULL;
    
    _GLFWjoystick* js = _glfw.joysticks + jid;
    if (!js->connected)
        return NULL;
    
    if (!js->motionInit)
    {
        _glfw.platform.createJoystickMotion(js);
    }
    
    return (GLFWmotion*) js->motion;
}

GLFWAPI const char* glfwGetGamepadName(int jid)
{
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);

    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (jid < 0 || jid > GLFW_JOYSTICK_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid joystick ID %i", jid);
        return NULL;
    }

    if (!initJoysticks())
        return NULL;

    js = _glfw.joysticks + jid;
    if (!js->connected)
        return NULL;

    if (!_glfw.platform.pollJoystick(js, _GLFW_POLL_PRESENCE))
        return NULL;

    if (!js->mapping)
        return NULL;

    return js->mapping->name;
}

GLFWAPI int glfwGetGamepadState(int jid, GLFWgamepadstate* state)
{
    int i;
    _GLFWjoystick* js;

    assert(jid >= GLFW_JOYSTICK_1);
    assert(jid <= GLFW_JOYSTICK_LAST);
    assert(state != NULL);

    memset(state, 0, sizeof(GLFWgamepadstate));

    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_FALSE);

    if (jid < 0 || jid > GLFW_JOYSTICK_LAST)
    {
        _glfwInputError(GLFW_INVALID_ENUM, "Invalid joystick ID %i", jid);
        return GLFW_FALSE;
    }

    if (!initJoysticks())
        return GLFW_FALSE;

    js = _glfw.joysticks + jid;
    if (!js->connected)
        return GLFW_FALSE;

    if (!_glfw.platform.pollJoystick(js, _GLFW_POLL_ALL))
        return GLFW_FALSE;

    if (!js->mapping)
        return GLFW_FALSE;

    for (i = 0;  i <= GLFW_GAMEPAD_BUTTON_LAST;  i++)
    {
        const _GLFWmapelement* e = js->mapping->buttons + i;
        if (e->type == _GLFW_JOYSTICK_AXIS)
        {
            const float value = js->axes[e->index] * e->axisScale + e->axisOffset;
            // HACK: This should be baked into the value transform
            // TODO: Bake into transform when implementing output modifiers
            if (e->axisOffset < 0 || (e->axisOffset == 0 && e->axisScale > 0))
            {
                if (value >= 0.f)
                    state->buttons[i] = GLFW_PRESS;
            }
            else
            {
                if (value <= 0.f)
                    state->buttons[i] = GLFW_PRESS;
            }
        }
        else if (e->type == _GLFW_JOYSTICK_HATBIT)
        {
            const unsigned int hat = e->index >> 4;
            const unsigned int bit = e->index & 0xf;
            if (js->hats[hat] & bit)
                state->buttons[i] = GLFW_PRESS;
        }
        else if (e->type == _GLFW_JOYSTICK_BUTTON)
            state->buttons[i] = js->buttons[e->index];
    }

    for (i = 0;  i <= GLFW_GAMEPAD_AXIS_LAST;  i++)
    {
        const _GLFWmapelement* e = js->mapping->axes + i;
        if (e->type == _GLFW_JOYSTICK_AXIS)
        {
            const float value = js->axes[e->index] * e->axisScale + e->axisOffset;
            state->axes[i] = fminf(fmaxf(value, -1.f), 1.f);
        }
        else if (e->type == _GLFW_JOYSTICK_HATBIT)
        {
            const unsigned int hat = e->index >> 4;
            const unsigned int bit = e->index & 0xf;
            if (js->hats[hat] & bit)
                state->axes[i] = 1.f;
            else
                state->axes[i] = -1.f;
        }
        else if (e->type == _GLFW_JOYSTICK_BUTTON)
            state->axes[i] = js->buttons[e->index] * 2.f - 1.f;
    }

    return GLFW_TRUE;
}

GLFWAPI void glfwSetClipboardString(GLFWwindow* handle, const char* string)
{
    assert(string != NULL);

    _GLFW_REQUIRE_INIT();
    _glfw.platform.setClipboardString(string);
}

GLFWAPI const char* glfwGetClipboardString(GLFWwindow* handle)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);
    return _glfw.platform.getClipboardString();
}

GLFWAPI double glfwGetTime(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0.0);
    return (double) (_glfwPlatformGetTimerValue() - _glfw.timer.offset) /
        _glfwPlatformGetTimerFrequency();
}

GLFWAPI void glfwSetTime(double time)
{
    _GLFW_REQUIRE_INIT();

    if (time != time || time < 0.0 || time > 18446744073.0)
    {
        _glfwInputError(GLFW_INVALID_VALUE, "Invalid time %f", time);
        return;
    }

    _glfw.timer.offset = _glfwPlatformGetTimerValue() -
        (uint64_t) (time * _glfwPlatformGetTimerFrequency());
}

GLFWAPI uint64_t glfwGetTimerValue(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);
    return _glfwPlatformGetTimerValue();
}

GLFWAPI uint64_t glfwGetTimerFrequency(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);
    return _glfwPlatformGetTimerFrequency();
}

