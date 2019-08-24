/*
  Falcon

  Drive LEDs on Millenium Falcon model.

  Author: Bob Mortensen
  Date: June 2019
*/

// #define DEBUG_OUTPUT
#include <Arduino.h>

unsigned long generateRandomSeed()
{
    unsigned long seed = 0;
    for (int i = 0; i < 8; ++i)
    {
        seed = seed << 4 | (analogRead(i) & 0x0f);
    }

    return seed;
}

class Led
{
    enum class LedMode
    {
        on,
        off,
        ramp,
        sinusoid,
        flicker
    };

    int pin;
    int period = 1000;
    int phase = 0;
    int delay = 0;
    LedMode mode = LedMode::sinusoid;

    int minBright = 0;
    int maxBright = 255;

    unsigned long startTime = 0;
    int lastValue = 0;

  public:
    Led(int pn)
        : pin(pn)
    {
    }

    void init()
    {
        pinMode(pin, OUTPUT);
        analogWrite(pin, 255);
        ::delay(250);
        analogWrite(pin, 0);
        startTime = millis();
    }

    void update(unsigned long now)
    {
        int delta = now - startTime - delay;
        if (delta < 0)
        {
            return;
        }

        int value;
        switch (mode)
        {
            case LedMode::off:
                value = 0;
                break;

            case LedMode::on:
                value = maxBright;
                break;

            case LedMode::ramp:
            {
                auto v = 1.0 * min(period, delta) / period;
                value = minBright + int((maxBright - minBright) * v);
                break;
            }

            case LedMode::sinusoid:
            {
                auto t = 1.0 * ((delta + phase) % period) / (period - 1);
                auto v = (cos(2 * M_PI * (t - 0.5)) + 1) / 2;
                value = minBright + int((maxBright - minBright) * v);
                break;
            }

            case LedMode::flicker:
            {
                value = lastValue;
                if (delta % 29 == 0)
                {
                    value = random(minBright, maxBright);
                }
            }
        }

        auto p2 = period / 2.0;
        auto factor = (p2 - delta) / p2;
        if (factor > 0)
        {
            int v = (factor * lastValue) + (1.0 - factor) * value;
            value = v;
        }

        if (value != lastValue)
        {
            analogWrite(pin, value);
        }

        lastValue = value;
    }

    void off()
    {
        mode = LedMode::off;
        startTime = millis();
        lastValue = 0;
        analogWrite(pin, lastValue);
    }

    void on(int mx)
    {
        mode = LedMode::on;
        maxBright = mx;
        lastValue = mx;
        startTime = millis();
        analogWrite(pin, lastValue);
    }

    void rampTo(int mx, int pd, int d=0)
    {
        mode = LedMode::ramp;
        minBright = lastValue;
        maxBright = mx;
        period = pd;
        delay = d;
        startTime = millis();
        update(millis());
    }

    void startSinusoid(int pd, int mn, int mx, int ph = 0)
    {
        mode = LedMode::sinusoid;
        minBright = mn;
        maxBright = mx;
        period = pd;
        phase = ph;
        startTime = millis();
        update(millis());
    }

    void startFlicker(int mn, int mx, int d)
    {
        mode = LedMode::flicker;
        minBright = mn;
        maxBright = mx;
        delay = d;
    }
};

enum class EngineState
{
    off,
    // starting,
    idling,
    fullPower,
    failing,
    rampingUp,
    rampingDown,
    landing
};

class Engine
{
    EngineState engineState = EngineState::idling;

    Led led1;
    Led led2;
    Led led3;

  public:
    Engine()
        : led1(9)
        , led2(10)
        , led3(11)
    {
    }

    void setup()
    {
        led1.init();
        led2.init();
        led3.init();
    }

    void newState(EngineState state)
    {
        engineState = state;
        switch (state)
        {
            case EngineState::off:
            {
                led1.off();
                led2.off();
                led3.off();
                break;
            }

            case EngineState::idling:
            {
                const int period = 2000;
                led1.startSinusoid(period, 10, 40);
                led2.startSinusoid(period, 10, 40, period / 3);
                led3.startSinusoid(period, 10, 40, -period / 3);
                break;
            }

            case EngineState::fullPower:
            {
                const int period = 60;
                led1.startSinusoid(period, 200, 255);
                led2.startSinusoid(period, 200, 255, period / 3);
                led3.startSinusoid(period, 200, 255, -period / 3);
                break;
            }

            case EngineState::failing:
            {
                led1.startFlicker(64, 128, 0);
                led2.startFlicker(0, 64, 0);
                led3.startSinusoid(1000, 64, 170, 0);
                break;
            }

            case EngineState::rampingUp:
            {
                led1.rampTo(220, 6000);
                led2.rampTo(220, 6000);
                led3.rampTo(220, 6000);
                break;
            }

            case EngineState::rampingDown:
            {
                led1.rampTo(0, 2000);
                led2.rampTo(0, 2000);
                led3.rampTo(0, 2000);
                break;
            }

            case EngineState::landing:
            {
                led1.rampTo(25, 4000);
                led2.rampTo(25, 4000);
                led3.rampTo(25, 4000);
                break;
            }
        }
    }

    void loop(unsigned int now)
    {
        led1.update(now);
        led2.update(now);
        led3.update(now);
    }
};

enum FalconState
{
    OnGround,
    PrepareForFlight,
    FailingStart,
    Failing,
    EmergencyShutdown,
    Restarting,
    InFlight,
    Landing
};


struct NextState
{
    long timeToSwitch;
    FalconState next;
};

Engine engine;
Led cockpit(3);
Led headlights(5);
Led landingLights(6);
unsigned stateStartTime;
FalconState falconState = FalconState::OnGround;
NextState nextState;
bool lastStartFailed = false;

NextState nextFalconState(FalconState state)
{
    falconState = state;
    switch (state)
    {
        case FalconState::OnGround:
        {
            cockpit.rampTo(255, 250);
            headlights.rampTo(0, 1000);
            landingLights.rampTo(255, 1000, 1000);
            engine.newState(EngineState::idling);
            lastStartFailed = !lastStartFailed && random(1024%4) == 0;
            return {
                random(5000, 20000),
                lastStartFailed ? FalconState::FailingStart : FalconState::PrepareForFlight
            };
        }

        case FalconState::PrepareForFlight:
        {
            cockpit.rampTo(64, 3000, 2000);
            headlights.rampTo(255, 500, 1400);
            landingLights.rampTo(0, 1500);
            engine.newState(EngineState::rampingUp);
            return {
                6000,
                FalconState::InFlight
            };
        }

        case FalconState::FailingStart:
        {
            cockpit.rampTo(64, 3000, 2000);
            headlights.rampTo(255, 500, 1400);
            landingLights.rampTo(0, 1500);
            engine.newState(EngineState::rampingUp);
            return {
                random(2000, 4000),
                FalconState::Failing
            };
        }

        case FalconState::Failing:
        {
            cockpit.startFlicker(0, 128, random(100, 1500));
            headlights.startFlicker(0, 32, random(1000, 2000));
            landingLights.startFlicker(32, 128, random(100, 2000));
            engine.newState(EngineState::failing);
            return {
                random(1000, 2000),
                FalconState::EmergencyShutdown
            };
        }

        case FalconState::EmergencyShutdown:
        {
            headlights.rampTo(0, 750);
            cockpit.rampTo(0, 250, 750);
            landingLights.rampTo(0, 500);
            engine.newState(EngineState::rampingDown);
            return {
                5000,
                FalconState::Restarting
            };
        }

        case FalconState::Restarting:
        {
            headlights.off();
            cockpit.rampTo(255, 750);
            landingLights.rampTo(255, 1500, 2000);
            engine.newState(EngineState::off);
            return {
                4000,
                FalconState::OnGround
            };
        }

        case FalconState::InFlight:
        {
            cockpit.rampTo(64, 250);
            headlights.rampTo(255, 500);
            landingLights.rampTo(0, 500);
            engine.newState(EngineState::fullPower);
            return {
                random(10000, 20000),
                FalconState::Landing
            };
        }

        case FalconState::Landing:
        {
            cockpit.rampTo(200, 500);
            landingLights.rampTo(255, 1500, 1500);
            headlights.rampTo(0, 2000, 1500);
            engine.newState(EngineState::landing);
            return {
                4000,
                FalconState::OnGround
            };
        }
    }

    return {5000, FalconState::OnGround};
}

void setup()
{
    randomSeed(generateRandomSeed());

    engine.setup();

    cockpit.init();
    headlights.init();
    landingLights.init();

    cockpit.on(255);
    headlights.off();
    landingLights.on(255);

    stateStartTime = millis();
    nextState = nextFalconState(FalconState::OnGround);
}

void loop()
{
    unsigned int now = millis();
    if (now - stateStartTime > nextState.timeToSwitch)
    {
        stateStartTime = now;
        nextState = nextFalconState(nextState.next);
    }

    engine.loop(now);
    cockpit.update(now);
    headlights.update(now);
    landingLights.update(now);
}
