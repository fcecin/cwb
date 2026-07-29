#pragma once

// The one accent, the one pill. Funnel-primary actions (Publish, Claim, Send)
// share this exact treatment so the brand cannot drift: green at rest, darker
// on hover, darker still pressed, pale when disabled. Machinery buttons
// (file manager verbs etc.) stay native Qt on purpose -- they are control
// surfaces, not invitations.
namespace cwbw {

inline const char* kPillCss =
    "QPushButton{background:#1a8917;color:#ffffff;border:none;"
    "border-radius:15px;padding:6px 20px;font-family:sans-serif;"
    "font-size:13px}"
    "QPushButton:hover{background:#156d12}"
    "QPushButton:pressed{background:#0f540e}"
    "QPushButton:disabled{background:#b3d9b2}";

}  // namespace cwbw
