#include "xr_internal.h"

#include <android/log.h>
#include <stdio.h>
#include <string.h>

static XrAction xr_make_action(const char* name, const char* label, XrActionType type, int hands) {
    XrActionCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = XR_TYPE_ACTION_CREATE_INFO;
    strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    info.actionType = type;
    if (hands) {
        info.countSubactionPaths = 2;
        info.subactionPaths = g_xr.hand_path;
    }
    XrAction action = XR_NULL_HANDLE;
    if (XR_FAILED(xrCreateAction(g_xr.action_set, &info, &action))) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "action %s failed", name);
        return XR_NULL_HANDLE;
    }
    return action;
}

typedef struct XrBind {
    XrAction action;
    char path[96];
} XrBind;

static void xr_add_bind(XrBind* list, int* count, int max, XrAction action, const char* path) {
    if (action == XR_NULL_HANDLE || *count >= max) {
        return;
    }
    list[*count].action = action;
    strncpy(list[*count].path, path, sizeof(list[*count].path) - 1);
    list[*count].path[sizeof(list[*count].path) - 1] = '\0';
    (*count)++;
}

static void xr_add_hand_bind(XrBind* list, int* count, int max, XrAction action, const char* suffix) {
    char path[96];
    snprintf(path, sizeof(path), "/user/hand/left/%s", suffix);
    xr_add_bind(list, count, max, action, path);
    snprintf(path, sizeof(path), "/user/hand/right/%s", suffix);
    xr_add_bind(list, count, max, action, path);
}

static void xr_suggest(const char* profile, const XrBind* list, int count) {
    XrPath profile_path = XR_NULL_PATH;
    if (XR_FAILED(xrStringToPath(g_xr.instance, profile, &profile_path))) {
        return;
    }
    XrActionSuggestedBinding bindings[32];
    int used = 0;
    for (int i = 0; i < count && used < 32; i++) {
        XrPath path = XR_NULL_PATH;
        if (XR_FAILED(xrStringToPath(g_xr.instance, list[i].path, &path))) {
            continue;
        }
        bindings[used].action = list[i].action;
        bindings[used].binding = path;
        used++;
    }
    if (used == 0) {
        return;
    }
    XrInteractionProfileSuggestedBinding suggested;
    memset(&suggested, 0, sizeof(suggested));
    suggested.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    suggested.interactionProfile = profile_path;
    suggested.countSuggestedBindings = (uint32_t)used;
    suggested.suggestedBindings = bindings;
    XrResult result = xrSuggestInteractionProfileBindings(g_xr.instance, &suggested);
    if (XR_FAILED(result)) {
        __android_log_print(ANDROID_LOG_WARN, "moonlight-xr", "binding profile skipped: %s (%d)", profile, (int)result);
    }
}

static void xr_suggest_standard(const char* profile) {
    XrBind list[32];
    memset(list, 0, sizeof(list));
    int count = 0;
    xr_add_hand_bind(list, &count, 32, g_xr.action_aim, "input/aim/pose");
    xr_add_hand_bind(list, &count, 32, g_xr.action_grip, "input/grip/pose");
    xr_add_hand_bind(list, &count, 32, g_xr.action_squeeze, "input/squeeze/value");
    xr_add_hand_bind(list, &count, 32, g_xr.action_trigger, "input/trigger/value");
    xr_add_hand_bind(list, &count, 32, g_xr.action_stick, "input/thumbstick");
    xr_add_hand_bind(list, &count, 32, g_xr.action_stick_click, "input/thumbstick/click");
    xr_add_bind(list, &count, 32, g_xr.action_x, "/user/hand/left/input/x/click");
    xr_add_bind(list, &count, 32, g_xr.action_y, "/user/hand/left/input/y/click");
    xr_add_bind(list, &count, 32, g_xr.action_menu, "/user/hand/left/input/menu/click");
    xr_add_bind(list, &count, 32, g_xr.action_a, "/user/hand/right/input/a/click");
    xr_add_bind(list, &count, 32, g_xr.action_b, "/user/hand/right/input/b/click");
    xr_suggest(profile, list, count);
}

static void xr_suggest_pico_g3(void) {
    XrBind list[24];
    memset(list, 0, sizeof(list));
    int count = 0;
    xr_add_hand_bind(list, &count, 24, g_xr.action_aim, "input/aim/pose");
    xr_add_hand_bind(list, &count, 24, g_xr.action_grip, "input/grip/pose");
    xr_add_hand_bind(list, &count, 24, g_xr.action_trigger, "input/trigger/value");
    xr_add_hand_bind(list, &count, 24, g_xr.action_stick, "input/thumbstick");
    xr_add_hand_bind(list, &count, 24, g_xr.action_stick_click, "input/thumbstick/click");
    xr_add_bind(list, &count, 24, g_xr.action_menu, "/user/hand/left/input/menu/click");
    xr_suggest("/interaction_profiles/bytedance/pico_g3_controller", list, count);
}

static int xr_make_space(XrAction action, int hand, XrSpace* out_space) {
    XrActionSpaceCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
    info.action = action;
    info.subactionPath = g_xr.hand_path[hand];
    info.poseInActionSpace.orientation.w = 1.0f;
    return XR_SUCCEEDED(xrCreateActionSpace(g_xr.session, &info, out_space));
}

void xr_input_init(void) {
    g_xr.input_ok = 0;
    if (XR_FAILED(xrStringToPath(g_xr.instance, "/user/hand/left", &g_xr.hand_path[0])) ||
        XR_FAILED(xrStringToPath(g_xr.instance, "/user/hand/right", &g_xr.hand_path[1]))) {
        return;
    }

    XrActionSetCreateInfo set_info;
    memset(&set_info, 0, sizeof(set_info));
    set_info.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    strncpy(set_info.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(set_info.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    if (XR_FAILED(xrCreateActionSet(g_xr.instance, &set_info, &g_xr.action_set))) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "xrCreateActionSet failed");
        return;
    }

    g_xr.action_aim = xr_make_action("aim_pose", "Aim Pose", XR_ACTION_TYPE_POSE_INPUT, 1);
    g_xr.action_grip = xr_make_action("grip_pose", "Grip Pose", XR_ACTION_TYPE_POSE_INPUT, 1);
    g_xr.action_squeeze = xr_make_action("squeeze", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, 1);
    g_xr.action_trigger = xr_make_action("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, 1);
    g_xr.action_stick = xr_make_action("thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, 1);
    g_xr.action_stick_click = xr_make_action("thumbstick_click", "Thumbstick Click", XR_ACTION_TYPE_BOOLEAN_INPUT, 1);
    g_xr.action_a = xr_make_action("click_a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    g_xr.action_b = xr_make_action("click_b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    g_xr.action_x = xr_make_action("click_x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    g_xr.action_y = xr_make_action("click_y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    g_xr.action_menu = xr_make_action("click_menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    if (g_xr.action_aim == XR_NULL_HANDLE || g_xr.action_grip == XR_NULL_HANDLE) {
        return;
    }

    static const char* profiles[] = {
            "/interaction_profiles/oculus/touch_controller",
            "/interaction_profiles/meta/touch_controller_plus",
            "/interaction_profiles/meta/touch_plus_controller",
            "/interaction_profiles/meta/touch_pro_controller",
            "/interaction_profiles/meta/touch_controller_quest_2",
            "/interaction_profiles/meta/touch_controller_quest_1_rift_s",
            "/interaction_profiles/bytedance/pico_neo3_controller",
            "/interaction_profiles/bytedance/pico4_controller",
    };
    for (int i = 0; i < (int)(sizeof(profiles) / sizeof(profiles[0])); i++) {
        xr_suggest_standard(profiles[i]);
    }
    xr_suggest_pico_g3();

    XrSessionActionSetsAttachInfo attach;
    memset(&attach, 0, sizeof(attach));
    attach.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    attach.countActionSets = 1;
    attach.actionSets = &g_xr.action_set;
    if (XR_FAILED(xrAttachSessionActionSets(g_xr.session, &attach))) {
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "xrAttachSessionActionSets failed");
        return;
    }

    for (int hand = 0; hand < 2; hand++) {
        if (!xr_make_space(g_xr.action_aim, hand, &g_xr.aim_space[hand]) ||
            !xr_make_space(g_xr.action_grip, hand, &g_xr.grip_space[hand])) {
            __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "action space failed");
            return;
        }
    }
    g_xr.input_ok = 1;
}

void xr_input_destroy(void) {
    for (int hand = 0; hand < 2; hand++) {
        if (g_xr.aim_space[hand] != XR_NULL_HANDLE) {
            xrDestroySpace(g_xr.aim_space[hand]);
            g_xr.aim_space[hand] = XR_NULL_HANDLE;
        }
        if (g_xr.grip_space[hand] != XR_NULL_HANDLE) {
            xrDestroySpace(g_xr.grip_space[hand]);
            g_xr.grip_space[hand] = XR_NULL_HANDLE;
        }
    }
    g_xr.input_ok = 0;
    g_xr.action_set = XR_NULL_HANDLE;
}

static int xr_read_bool(XrAction action, XrPath sub) {
    if (action == XR_NULL_HANDLE) {
        return 0;
    }
    XrActionStateGetInfo get;
    memset(&get, 0, sizeof(get));
    get.type = XR_TYPE_ACTION_STATE_GET_INFO;
    get.action = action;
    get.subactionPath = sub;
    XrActionStateBoolean state;
    memset(&state, 0, sizeof(state));
    state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    if (XR_FAILED(xrGetActionStateBoolean(g_xr.session, &get, &state)) || !state.isActive) {
        return 0;
    }
    return state.currentState == XR_TRUE;
}

static float xr_read_float(XrAction action, XrPath sub) {
    if (action == XR_NULL_HANDLE) {
        return 0.0f;
    }
    XrActionStateGetInfo get;
    memset(&get, 0, sizeof(get));
    get.type = XR_TYPE_ACTION_STATE_GET_INFO;
    get.action = action;
    get.subactionPath = sub;
    XrActionStateFloat state;
    memset(&state, 0, sizeof(state));
    state.type = XR_TYPE_ACTION_STATE_FLOAT;
    if (XR_FAILED(xrGetActionStateFloat(g_xr.session, &get, &state)) || !state.isActive) {
        return 0.0f;
    }
    return state.currentState;
}

static int xr_read_stick(XrAction action, XrPath sub, float* x, float* y) {
    *x = 0.0f;
    *y = 0.0f;
    if (action == XR_NULL_HANDLE) {
        return 0;
    }
    XrActionStateGetInfo get;
    memset(&get, 0, sizeof(get));
    get.type = XR_TYPE_ACTION_STATE_GET_INFO;
    get.action = action;
    get.subactionPath = sub;
    XrActionStateVector2f state;
    memset(&state, 0, sizeof(state));
    state.type = XR_TYPE_ACTION_STATE_VECTOR2F;
    if (XR_FAILED(xrGetActionStateVector2f(g_xr.session, &get, &state)) || !state.isActive) {
        return 0;
    }
    *x = state.currentState.x;
    *y = state.currentState.y;
    return 1;
}

static int xr_locate_action(XrSpace space, XrTime time, XrPosef* pose) {
    if (space == XR_NULL_HANDLE) {
        return 0;
    }
    XrSpaceLocation location;
    memset(&location, 0, sizeof(location));
    location.type = XR_TYPE_SPACE_LOCATION;
    if (XR_FAILED(xrLocateSpace(space, g_xr.ref_space, time, &location))) {
        return 0;
    }
    if ((location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0 ||
        (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) {
        return 0;
    }
    *pose = location.pose;
    return 1;
}

void xr_input_sync(XrTime display_time) {
    for (int hand = 0; hand < 2; hand++) {
        g_xr.hand_active[hand] = 0;
        g_xr.grip_valid[hand] = 0;
        g_xr.squeeze_value[hand] = 0.0f;
        g_xr.trigger_value[hand] = 0.0f;
        g_xr.stick_x[hand] = 0.0f;
        g_xr.stick_y[hand] = 0.0f;
        g_xr.stick_click[hand] = 0;
    }
    g_xr.btn_a = g_xr.btn_b = g_xr.btn_x = g_xr.btn_y = g_xr.btn_menu = 0;
    if (!g_xr.input_ok || g_xr.session_state != XR_SESSION_STATE_FOCUSED) {
        return;
    }

    XrActiveActionSet active;
    active.actionSet = g_xr.action_set;
    active.subactionPath = XR_NULL_PATH;
    XrActionsSyncInfo sync;
    memset(&sync, 0, sizeof(sync));
    sync.type = XR_TYPE_ACTIONS_SYNC_INFO;
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(g_xr.session, &sync))) {
        return;
    }

    for (int hand = 0; hand < 2; hand++) {
        XrActionStateGetInfo get;
        memset(&get, 0, sizeof(get));
        get.type = XR_TYPE_ACTION_STATE_GET_INFO;
        get.action = g_xr.action_aim;
        get.subactionPath = g_xr.hand_path[hand];
        XrActionStatePose pose_state;
        memset(&pose_state, 0, sizeof(pose_state));
        pose_state.type = XR_TYPE_ACTION_STATE_POSE;
        if (XR_FAILED(xrGetActionStatePose(g_xr.session, &get, &pose_state)) || !pose_state.isActive) {
            continue;
        }
        if (!xr_locate_action(g_xr.aim_space[hand], display_time, &g_xr.aim_pose[hand])) {
            continue;
        }
        g_xr.hand_active[hand] = 1;
        g_xr.grip_valid[hand] = xr_locate_action(g_xr.grip_space[hand], display_time, &g_xr.grip_pose[hand]);
        g_xr.squeeze_value[hand] = xr_read_float(g_xr.action_squeeze, g_xr.hand_path[hand]);
        g_xr.trigger_value[hand] = xr_read_float(g_xr.action_trigger, g_xr.hand_path[hand]);
        xr_read_stick(g_xr.action_stick, g_xr.hand_path[hand], &g_xr.stick_x[hand], &g_xr.stick_y[hand]);
        g_xr.stick_click[hand] = xr_read_bool(g_xr.action_stick_click, g_xr.hand_path[hand]);
    }
    g_xr.btn_a = xr_read_bool(g_xr.action_a, XR_NULL_PATH);
    g_xr.btn_b = xr_read_bool(g_xr.action_b, XR_NULL_PATH);
    g_xr.btn_x = xr_read_bool(g_xr.action_x, XR_NULL_PATH);
    g_xr.btn_y = xr_read_bool(g_xr.action_y, XR_NULL_PATH);
    g_xr.btn_menu = xr_read_bool(g_xr.action_menu, XR_NULL_PATH);
}
