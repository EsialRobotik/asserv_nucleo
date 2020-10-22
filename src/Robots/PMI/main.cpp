#include "ch.h"
#include "hal.h"
#include "shell.h"
#include <chprintf.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cfloat>

#include "raspIO.h"
#include "util/asservMath.h"
#include "util/chibiOsAllocatorWrapper.h"
#include "AsservMain.h"
#include "commandManager/CommandManager.h"
#include "SpeedController/SpeedController.h"
#include "SpeedController/AdaptativeSpeedController.h"
#include "SpeedController/SpeedController.h"
#include "Encoders/QuadratureEncoder.h"
#include "motorController/Md22.h"
#include "Odometry.h"
#include "USBStream.h"
#include "AccelerationLimiter/SimpleAccelerationLimiter.h"
#include "AccelerationLimiter/AdvancedAccelerationLimiter.h"
#include "Pll.h"



#define ASSERV_THREAD_FREQUENCY (500)
#define ASSERV_THREAD_PERIOD_S (1.0/ASSERV_THREAD_FREQUENCY)
#define ASSERV_POSITION_DIVISOR (5)

#define ENCODERS_WHEELS_RADIUS_MM (31.80/2.0)
#define ENCODERS_WHEELS_DISTANCE_MM (264)
#define ENCODERS_TICKS_BY_TURN (1440*4)

#define MAX_SPEED_MM_PER_SEC (1500)

#define DIST_REGULATOR_KP (3)
#define DIST_REGULATOR_MAX_ACC ((3.0)/ASSERV_THREAD_PERIOD_S)
#define DIST_REGULATOR_MAX_ACC_LOW_SPEED (1/ASSERV_THREAD_PERIOD_S)
#define DIST_REGULATOR_LOW_SPEED_THRESHOLD (300)

#define ANGLE_REGULATOR_KP (650)
#define ANGLE_REGULATOR_MAX_ACC (3/ASSERV_THREAD_PERIOD_S)

//float speed_controller_right_Kp[NB_PI_SUBSET] = { 0.1, 0.1, 0.1};
//float speed_controller_right_Ki[NB_PI_SUBSET] = { 1.0, 0.8, 0.6};
//float speed_controller_right_SpeedRange[NB_PI_SUBSET] = { 0, 20, 50};
//
//float speed_controller_left_Kp[NB_PI_SUBSET] = { 0.1, 0.1, 0.1};
//float speed_controller_left_Ki[NB_PI_SUBSET] = { 1.0, 0.8, 0.6};
//float speed_controller_left_SpeedRange[NB_PI_SUBSET] = { 0, 20, 50};

float speed_controller_right_Kp = 0.1;
float speed_controller_right_Ki = 0.6;

float speed_controller_left_Kp = 0.1;
float speed_controller_left_Ki = 0.6;


#define PLL_BANDWIDTH (250)


#define COMMAND_MANAGER_ARRIVAL_ANGLE_THRESHOLD_RAD (1)
#define COMMAND_MANAGER_ARRIVAL_DISTANCE_THRESHOLD_mm (30)
#define COMMAND_MANAGER_GOTO_ANGLE_THRESHOLD_RAD (M_PI/8)
#define COMMAND_MANAGER_GOTONOSTOP_FULLSPEED_CONSIGN_DIST_mm (MAX_SPEED_MM_PER_SEC/DIST_REGULATOR_KP)
#define COMMAND_MANAGER_GOTONOSTOP_MIN_DIST_NEXT_CONSIGN_mm (200)
#define COMMAND_MANAGER_GOTONOSTOP_NEXT_FULLSPEED_CONSIGN_ANGLE_mm (M_PI/8)




QuadratureEncoder encoders(true, true, true);
Md22::I2cPinInit ESIALCardPinConf_SCL_SDA = {GPIOB, 6, GPIOB, 7};
Md22 md22MotorController(true, true, true, &ESIALCardPinConf_SCL_SDA, 100000);

Regulator angleRegulator(ANGLE_REGULATOR_KP, MAX_SPEED_MM_PER_SEC);
Regulator distanceRegulator(DIST_REGULATOR_KP, MAX_SPEED_MM_PER_SEC);

Odometry odometry(ENCODERS_WHEELS_DISTANCE_MM, 0, 0);

SpeedController speedControllerRight(speed_controller_right_Kp, speed_controller_right_Ki, 100, MAX_SPEED_MM_PER_SEC, ASSERV_THREAD_FREQUENCY);
SpeedController speedControllerLeft(speed_controller_left_Kp, speed_controller_left_Ki, 100, MAX_SPEED_MM_PER_SEC, ASSERV_THREAD_FREQUENCY);
//AdaptativeSpeedController speedControllerRight(speed_controller_right_Kp, speed_controller_right_Ki, speed_controller_right_SpeedRange, 100, MAX_SPEED_MM_PER_SEC, ASSERV_THREAD_FREQUENCY);
//AdaptativeSpeedController speedControllerLeft(speed_controller_left_Kp, speed_controller_left_Ki, speed_controller_left_SpeedRange, 100, MAX_SPEED_MM_PER_SEC, ASSERV_THREAD_FREQUENCY);

Pll rightPll(PLL_BANDWIDTH);
Pll leftPll(PLL_BANDWIDTH);

SimpleAccelerationLimiter angleAccelerationlimiter(ANGLE_REGULATOR_MAX_ACC);
AdvancedAccelerationLimiter distanceAccelerationLimiter(DIST_REGULATOR_MAX_ACC, DIST_REGULATOR_MAX_ACC_LOW_SPEED, DIST_REGULATOR_LOW_SPEED_THRESHOLD);

CommandManager commandManager( COMMAND_MANAGER_ARRIVAL_ANGLE_THRESHOLD_RAD, COMMAND_MANAGER_ARRIVAL_DISTANCE_THRESHOLD_mm,
                               COMMAND_MANAGER_GOTO_ANGLE_THRESHOLD_RAD,
                               COMMAND_MANAGER_GOTONOSTOP_FULLSPEED_CONSIGN_DIST_mm, COMMAND_MANAGER_GOTONOSTOP_MIN_DIST_NEXT_CONSIGN_mm, COMMAND_MANAGER_GOTONOSTOP_NEXT_FULLSPEED_CONSIGN_ANGLE_mm,
                               angleRegulator, distanceRegulator);

AsservMain mainAsserv( ASSERV_THREAD_FREQUENCY, ASSERV_POSITION_DIVISOR,
                       ENCODERS_WHEELS_RADIUS_MM, ENCODERS_WHEELS_DISTANCE_MM, ENCODERS_TICKS_BY_TURN,
                       commandManager, md22MotorController, encoders, odometry,
                       angleRegulator, distanceRegulator,
                       angleAccelerationlimiter, distanceAccelerationLimiter,
                       speedControllerRight, speedControllerLeft,
                       rightPll, leftPll);


/*
 *  As the dynamic allocation is disabled after init,
 *  use this semaphore to ensure that init is finished before
 *  disabling further dynamic allocation
 */
static binary_semaphore_t asservStarted_semaphore;

static THD_WORKING_AREA(waAsservThread, 512);
static THD_FUNCTION(AsservThread, arg)
{
    (void) arg;
    chRegSetThreadName("AsservThread");

    md22MotorController.init();
    encoders.init();
    encoders.start();
    USBStream::init();

    chBSemSignal(&asservStarted_semaphore);

    mainAsserv.mainLoop();
}


THD_WORKING_AREA(wa_shell, 2048);
THD_WORKING_AREA(wa_controlPanel, 256);
THD_FUNCTION(ControlPanelThread, p);

char history_buffer[SHELL_MAX_HIST_BUFF];
char *completion_buffer[SHELL_MAX_COMPLETIONS];

void asservCommandUSB(BaseSequentialStream *chp, int argc, char **argv);

void asservCommandSerial();


BaseSequentialStream *outputStream;
int main(void)
{
    halInit();
    chSysInit();

    sdStart(&SD2, NULL);
    shellInit();

    chBSemObjectInit(&asservStarted_semaphore, true);
    chThdCreateStatic(waAsservThread, sizeof(waAsservThread), HIGHPRIO, AsservThread, NULL);
    chBSemWait(&asservStarted_semaphore);

    outputStream = reinterpret_cast<BaseSequentialStream*>(&SD2);

    // Custom commands
    const ShellCommand shellCommands[] = { { "asserv", &(asservCommandUSB) }, { nullptr, nullptr } };
    ShellConfig shellCfg =
    {
        /* sc_channel */outputStream,
        /* sc_commands */shellCommands,
#if (SHELL_USE_HISTORY == TRUE)
        /* sc_histbuf */history_buffer,
        /* sc_histsize */sizeof(history_buffer),
#endif
#if (SHELL_USE_COMPLETION == TRUE)
        /* sc_completion */completion_buffer
#endif
    };

#ifdef ENABLE_SHELL
    bool startShell = true;
#else
    bool startShell = false;
#endif
    if (startShell)
    {
        thread_t *shellThd = chThdCreateStatic(wa_shell, sizeof(wa_shell), LOWPRIO, shellThread, &shellCfg);
        chRegSetThreadNameX(shellThd, "shell");

        // Le thread controlPanel n'a de sens que quand le shell tourne
        thread_t *controlPanelThd = chThdCreateStatic(wa_controlPanel, sizeof(wa_controlPanel), LOWPRIO, ControlPanelThread, nullptr);
        chRegSetThreadNameX(controlPanelThd, "controlPanel");
    }
    else
    {
        thread_t *asserCmdSerialThread = chThdCreateStatic(wa_shell, sizeof(wa_shell), LOWPRIO, asservCommandSerial, nullptr);
        chRegSetThreadNameX(asserCmdSerialThread, "asserv Command serial");

        thread_t *controlPanelThd = chThdCreateStatic(wa_controlPanel, sizeof(wa_controlPanel), LOWPRIO, asservPositionSerial, nullptr);
        chRegSetThreadNameX(controlPanelThd, "asserv position update serial");

    }

    deactivateHeapAllocation();

    chThdSetPriority(LOWPRIO);
    while (true)
    {
        palClearPad(GPIOA, GPIOA_LED_GREEN);
        chThdSleepMilliseconds(250);
        palSetPad(GPIOA, GPIOA_LED_GREEN);
        chThdSleepMilliseconds(250);
    }
}


void asservCommandUSB(BaseSequentialStream *chp, int argc, char **argv)
{
    auto printUsage = []()
    {
        chprintf(outputStream,"Usage :");
        chprintf(outputStream," - asserv enablemotor 0|1\r\n");
        chprintf(outputStream," - asserv enablepolar 0|1\r\n");
        chprintf(outputStream," - asserv coders \r\n");
        chprintf(outputStream," - asserv reset \r\n");
        chprintf(outputStream," - asserv motorspeed [r|l] speed \r\n");
        chprintf(outputStream," -------------- \r\n");
        chprintf(outputStream," - asserv wheelspeedstep [r|l] [speed] [step time] \r\n");
        chprintf(outputStream," -------------- \r\n");
        chprintf(outputStream," - asserv robotfwspeedstep [speed] [step time] \r\n");
        chprintf(outputStream," - asserv robotangspeedstep [speed] [step time] \r\n");
        chprintf(outputStream," - asserv speedcontrol [r|l] [Kp] [Ki] \r\n");
        chprintf(outputStream," - asserv angleacc delta_speed \r\n");
        chprintf(outputStream," - asserv distacc delta_speed \r\n");
        chprintf(outputStream," ------------------- \r\n");
        chprintf(outputStream," - asserv addangle angle_rad \r\n");
        chprintf(outputStream," - asserv anglereset\r\n");
        chprintf(outputStream," - asserv anglecontrol Kp\r\n");
        chprintf(outputStream," ------------------- \r\n");
        chprintf(outputStream," - asserv adddist mm \r\n");
        chprintf(outputStream," - asserv distreset\r\n");
        chprintf(outputStream," - asserv distcontrol Kp\r\n");
        chprintf(outputStream," -------------- \r\n");
        chprintf(outputStream," - asserv addgoto X Y\r\n");
        chprintf(outputStream," - asserv gototest\r\n");
    };
    (void) chp;

    if (argc == 0)
    {
        printUsage();
        return;
    }

    if (!strcmp(argv[0], "wheelspeedstep"))
    {
        char side = *argv[1];
        float speedGoal = atof(argv[2]);
        int time = atoi(argv[3]);
        chprintf(outputStream, "setting fw robot speed %.2f rad/s for %d ms\r\n", speedGoal, time);

        chprintf(outputStream, "setting wheel %s to speed %.2f rad/s for %d ms \r\n", (side == 'r') ? "right" : "left", speedGoal, time);

        float speedRight = speedGoal;
        float speedLeft = 0;
        if (side == 'l')
        {
            speedLeft = speedGoal;
            speedRight = 0;
        }

        mainAsserv.setWheelsSpeed(speedRight, speedLeft);
        chThdSleepMilliseconds(time);
        mainAsserv.setWheelsSpeed(0, 0);
    }
    else if (!strcmp(argv[0], "robotfwspeedstep"))
    {
        float speedGoal = atof(argv[1]);
        int time = atoi(argv[2]);
        chprintf(outputStream, "setting fw robot speed %.2f rad/s for %d ms\r\n", speedGoal, time);

        mainAsserv.setRegulatorsSpeed(speedGoal, 0);
        chThdSleepMilliseconds(time);
        mainAsserv.setRegulatorsSpeed(0, 0);
    }
    else if (!strcmp(argv[0], "robotangspeedstep"))
    {
        float speedGoal = atof(argv[1]);
        int time = atoi(argv[2]);
        chprintf(outputStream, "setting angle robot speed %.2f rad/s for %d ms\r\n", speedGoal, time);

        mainAsserv.setRegulatorsSpeed(0, speedGoal);
        chThdSleepMilliseconds(time);
        mainAsserv.setRegulatorsSpeed(0, 0);
    }
    else if (!strcmp(argv[0], "speedcontrol"))
    {
        char side = *argv[1];
        float Kp = atof(argv[2]);
        float Ki = atof(argv[3]);
        uint8_t range =  0;

        chprintf(outputStream, "setting speed control Kp:%.2f Ki:%.2f range:%d to side %c \r\n", Kp, Ki, range, side);

        if (side == 'r')
            speedControllerRight.setGains(Kp, Ki);
        else if (side == 'l')
            speedControllerLeft.setGains(Kp, Ki);
    }
    else if (!strcmp(argv[0], "angleacc"))
    {
        float acc = atof(argv[1]);
        chprintf(outputStream, "setting angle slope delta %.2f \r\n", acc);

        angleAccelerationlimiter.setMaxAcceleration(acc);
    }
    else if (!strcmp(argv[0], "distacc"))
    {
        float acc = atof(argv[1]);
        chprintf(outputStream, "setting distance slope delta %.2f \r\n", acc);

        distanceAccelerationLimiter.setMaxAcceleration(acc);
    }
    else if (!strcmp(argv[0], "addangle"))
    {
        float angle = atof(argv[1]);
        chprintf(outputStream, "Adding angle %.2frad \r\n", angle);

        mainAsserv.resetToNormalMode();
        commandManager.addTurn(angle);
    }
    else if (!strcmp(argv[0], "anglereset"))
    {
        chprintf(outputStream, "Reseting angle accumulator \r\n");
        angleRegulator.reset();
    }
    else if (!strcmp(argv[0], "distreset"))
    {
        chprintf(outputStream, "Reseting distance accumulator \r\n");
        distanceRegulator.reset();
    }
    else if (!strcmp(argv[0], "adddist"))
    {
        float dist = atof(argv[1]);
        chprintf(outputStream, "Adding distance %.2fmm \r\n", dist);

        mainAsserv.resetToNormalMode();
        commandManager.addStraightLine(dist);
    }
    else if (!strcmp(argv[0], "anglecontrol"))
    {
        float Kp = atof(argv[1]);
        chprintf(outputStream, "setting angle Kp to %.2f \r\n", Kp);

        angleRegulator.setGain(Kp);
    }
    else if (!strcmp(argv[0], "distcontrol"))
    {
        float Kp = atof(argv[1]);
        chprintf(outputStream, "setting dist Kp to %.2f \r\n", Kp);

        distanceRegulator.setGain(Kp);
    }
    else if (!strcmp(argv[0], "enablemotor"))
    {
        bool enable = !(atoi(argv[1]) == 0);
        chprintf(outputStream, "%s motor output\r\n", (enable ? "enabling" : "disabling"));
        if( enable )
            mainAsserv.resetEmergencyStop();
        else
            mainAsserv.setEmergencyStop();
    }
    else if (!strcmp(argv[0], "coders"))
    {
        int32_t encoderRight, encoderLeft;
        encoders.getEncodersTotalCount(&encoderRight, &encoderLeft);
        chprintf(outputStream, "Encoders count %d %d \r\n", encoderRight, encoderLeft);
    }
    else if (!strcmp(argv[0], "reset"))
    {
        mainAsserv.reset();
        chprintf(outputStream, "asserv resetted \r\n");
    }
    else if (!strcmp(argv[0], "motorspeed"))
    {
        char side = *argv[1];
        float speedGoal = atof(argv[2]);

        chprintf(outputStream, "setting wheel %s to speed %.2f \r\n", (side == 'r') ? "right" : "left", speedGoal);

        if (side == 'l')
            md22MotorController.setMotorLeftSpeed(speedGoal);
        else
            md22MotorController.setMotorRightSpeed(speedGoal);
    }
    else if (!strcmp(argv[0], "enablepolar"))
    {
        bool enable = !(atoi(argv[1]) == 0);
        chprintf(outputStream, "%s polar control\r\n", (enable ? "enabling" : "disabling"));

        mainAsserv.enablePolar(enable);

    }
    else if (!strcmp(argv[0], "addgoto"))
    {
        float X = atof(argv[1]);
        float Y = atof(argv[2]);
        chprintf(outputStream, "Adding goto(%.2f,%.2f) consign\r\n", X, Y);

        mainAsserv.resetToNormalMode();
        commandManager.addGoTo(X, Y);
    }
    else if (!strcmp(argv[0], "gototest"))
    {
        mainAsserv.resetToNormalMode();
        commandManager.addGoToNoStop(500, 0);
        commandManager.addGoToNoStop(900, 0);
        commandManager.addGoToNoStop(1100, 0);
        commandManager.addGoToNoStop(1100, 200);
        commandManager.addGoToNoStop(1100, 400);
        commandManager.addGoToNoStop(900, 400);
        commandManager.addGoToNoStop(500, 400);
        commandManager.addGoToNoStop(100, 200);
        commandManager.addGoToAngle(500, 200);


    }
    else
    {
        printUsage();
    }
}


THD_FUNCTION(ControlPanelThread, p)
{
    (void) p;
    void *ptr = nullptr;
    uint32_t size = 0;
    char *firstArg = nullptr;
    char *argv[7];
    while (!chThdShouldTerminateX())
    {
        USBStream::instance()->getFullBuffer(&ptr, &size);
        if (size > 0)
        {
            char *buffer = (char*) ptr;

            /*
             *  On transforme la commande recu dans une version argv/argc
             *    de manière a utiliser les commandes shell déjà définie...
             */
            bool prevWasSpace = false;
            firstArg = buffer;
            int nb_arg = 0;
            for (uint32_t i = 0; i < size; i++)
            {
                if (prevWasSpace && buffer[i] != ' ')
                {
                    argv[nb_arg++] = &buffer[i];
                }

                if (buffer[i] == ' ' || buffer[i] == '\r' || buffer[i] == '\n')
                {
                    prevWasSpace = true;
                    buffer[i] = '\0';
                }
                else
                {
                    prevWasSpace = false;
                }
            }

            // On évite de faire appel au shell si le nombre d'arg est mauvais ou si la 1ière commande est mauvaise...
            if (nb_arg > 0 && !strcmp(firstArg, "asserv"))
            {
                asservCommandUSB(nullptr, nb_arg, argv);
            }
            USBStream::instance()->releaseBuffer();
        }
    }
}
