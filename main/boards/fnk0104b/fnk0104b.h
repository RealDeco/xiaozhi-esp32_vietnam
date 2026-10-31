#ifndef _FNK0104B_H_
#define _FNK0104B_H_

#include "board.h"
#include "display/display.h"
#include "display/lcd_touch.h"
#include "backlight.h"

class FNK0104B : public Board {
public:
    FNK0104B();
    ~FNK0104B() override;

    void Initialize() override;
    void SetChatState(int state) override;
    void StartNetwork() override;
    void ResetWifiConfiguration() override;

private:
    Display* display = nullptr;
    LcdTouch* touch = nullptr;
    Backlight* backlight = nullptr;
};

#endif // _FNK0104B_H_
