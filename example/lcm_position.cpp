/*****************************************************************
 Copyright (c) 2020, Unitree Robotics.Co.Ltd. All rights reserved.
******************************************************************/

#include "unitree_legged_sdk/unitree_legged_sdk.h"
#include "unitree_legged_sdk/joystick.h"
#include "unitree_legged_sdk/go1_const.h"
#include <math.h>
#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <thread>
#include <lcm/lcm-cpp.hpp>
#include "state_estimator_lcmt.hpp"
#include "leg_control_data_lcmt.hpp"
#include "pd_tau_targets_lcmt.hpp"
#include "rc_command_lcmt.hpp"

using namespace std;
using namespace UNITREE_LEGGED_SDK;


constexpr double go1_Hip_max_mod   = 0.8; //1.047;    // unit:radian ( = 60   degree)
constexpr double go1_Hip_min_mod   = -0.8; //-1.047;   // unit:radian ( = -60  degree)
constexpr double go1_Thigh_max_mod = 2.966;    // unit:radian ( = 170  degree)
constexpr double go1_Thigh_min_mod = -0.6; //-0.663;   // unit:radian ( = -38  degree)
constexpr double go1_Calf_max_mod  = -0.9; //-0.837;   // unit:radian ( = -48  degree)
constexpr double go1_Calf_min_mod  = -2.55;   //2.721 unit:radian ( = -156 degree)

class Custom
{
public:
    Custom(uint8_t level): safe(LeggedType::Go1), udp(level, 8090, "192.168.123.10", 8007) {
        udp.InitCmdData(cmd);
    }
    void UDPRecv();
    void UDPSend();
    void RobotControl();
    void init();
    void handleActionLCM(const lcm::ReceiveBuffer *rbuf, const std::string & chan, const pd_tau_targets_lcmt * msg);
    void _simpleLCMThread();

    Safety safe;
    UDP udp;
    LowCmd cmd = {0};
    LowState state = {0};
    // float qInit[3]={0};
    // float qDes[3]={0};
    // float sin_mid_q[3] = {0.0, 1.2, -2.0};
    // float Kp[3] = {0};
    // float Kd[3] = {0};
    // double time_consume = 0;
    // int rate_count = 0;
    // int sin_count = 0;
    int motiontime = 0;
    float dt = 0.002;     // 0.001~0.01

    lcm::LCM _simpleLCM;
    std::thread _simple_LCM_thread;
    bool _firstCommandReceived;
    bool _firstRun;
    state_estimator_lcmt body_state_simple = {0};
    leg_control_data_lcmt joint_state_simple = {0};
    pd_tau_targets_lcmt joint_command_simple = {0};
    rc_command_lcmt rc_command = {0};

    xRockerBtnDataStruct _keyData;
    int mode = 0;

};

void Custom::init()
{
    _simpleLCM.subscribe("pd_plustau_targets", &Custom::handleActionLCM, this);
    _simple_LCM_thread = std::thread(&Custom::_simpleLCMThread, this);

    _firstCommandReceived = false;
    _firstRun = true;

    // set nominal pose

    for(int i = 0; i < 12; i++){
        joint_command_simple.qd_des[i] = 0;
        joint_command_simple.tau_ff[i] = 0;
        // joint_command_simple.kp[i] = 20.;
        joint_command_simple.kp[i] = 10; // set it lower to avoid over-current shutdown at startup??
        joint_command_simple.kd[i] = 0.5;
    }

    joint_command_simple.q_des[0] = 0.1;
    joint_command_simple.q_des[1] = 0.8;
    joint_command_simple.q_des[2] = -1.5;
    joint_command_simple.q_des[3] = -0.1;
    joint_command_simple.q_des[4] = 0.8;
    joint_command_simple.q_des[5] = -1.5;
    joint_command_simple.q_des[6] = 0.1;
    joint_command_simple.q_des[7] = 1.0;
    joint_command_simple.q_des[8] = -1.5;
    joint_command_simple.q_des[9] = -0.1;
    joint_command_simple.q_des[10] = 0.8;
    joint_command_simple.q_des[11] = -1.5;

    printf("SET NOMINAL POSE");


}

void Custom::UDPRecv()
{
    udp.Recv();
}

void Custom::UDPSend()
{
    udp.Send();
}

double jointLinearInterpolation(double initPos, double targetPos, double rate)
{
    double p;
    rate = std::min(std::max(rate, 0.0), 1.0);
    p = initPos*(1-rate) + targetPos*rate;
    return p;
}

void Custom::handleActionLCM(const lcm::ReceiveBuffer *rbuf, const std::string & chan, const pd_tau_targets_lcmt * msg){
    (void) rbuf;
    (void) chan;

    joint_command_simple = *msg;
    _firstCommandReceived = true;

}

void Custom::_simpleLCMThread(){
    while(true){
        _simpleLCM.handle();
    }
}

void Custom::RobotControl()
{
    motiontime++;
    udp.GetRecv(state);

    memcpy(&_keyData, &state.wirelessRemote[0], 40);

    rc_command.left_stick[0] = _keyData.lx;
    rc_command.left_stick[1] = _keyData.ly;
    rc_command.right_stick[0] = _keyData.rx;
    rc_command.right_stick[1] = _keyData.ry;
    rc_command.right_lower_right_switch = _keyData.btn.components.R2;
    rc_command.right_upper_switch = _keyData.btn.components.R1;
    rc_command.left_lower_left_switch = _keyData.btn.components.L2;
    rc_command.left_upper_switch = _keyData.btn.components.L1;


    if(_keyData.btn.components.A > 0){
        mode = 0;
    } else if(_keyData.btn.components.B > 0){
        mode = 1;
    }else if(_keyData.btn.components.X > 0){
        mode = 2;
    }else if(_keyData.btn.components.Y > 0){
        mode = 3;
    }else if(_keyData.btn.components.up > 0){
        mode = 4;
    }else if(_keyData.btn.components.right > 0){
        mode = 5;
    }else if(_keyData.btn.components.down > 0){
        mode = 6;
    }else if(_keyData.btn.components.left > 0){
        mode = 7;
    }

    rc_command.mode = mode;


    // publish state to LCM
    for(int i = 0; i < 12; i++){
        joint_state_simple.q[i] = state.motorState[i].q;
        joint_state_simple.qd[i] = state.motorState[i].dq;
        joint_state_simple.tau_est[i] = state.motorState[i].tauEst;
    }
    for(int i = 0; i < 4; i++){
        body_state_simple.quat[i] = state.imu.quaternion[i];
    }
    for(int i = 0; i < 3; i++){
        body_state_simple.rpy[i] = state.imu.rpy[i];
        body_state_simple.aBody[i] = state.imu.accelerometer[i];
        body_state_simple.omegaBody[i] = state.imu.gyroscope[i];
    }
    for(int i = 0; i < 4; i++){
        body_state_simple.contact_estimate[i] = state.footForce[i];
    }

    _simpleLCM.publish("state_estimator_data", &body_state_simple);
    _simpleLCM.publish("leg_control_data", &joint_state_simple);
    _simpleLCM.publish("rc_command", &rc_command);

    if(_firstRun && joint_state_simple.q[0] != 0){
        for(int i = 0; i < 12; i++){
            joint_command_simple.q_des[i] = joint_state_simple.q[i];
        }

        // the next two lines are enough to fuck the robot (but not singularly)
        //joint_command_simple.q_des[FR_0] = -0.5;
        //joint_command_simple.q_des[FL_0] = 0.5;        
        //joint_command_simple.q_des[RR_0] = -0.5;
        //joint_command_simple.q_des[RL_0] = 0.5;

        // joint_command_simple.q_des[FR_1] =  2.0;
        // joint_command_simple.q_des[FL_1] =  2.0;
        // joint_command_simple.q_des[RR_1] =  2.0;
        // joint_command_simple.q_des[RL_1] =  2.0;

        // joint_command_simple.q_des[FR_2] = -2.4;
        // joint_command_simple.q_des[FL_2] = -2.4;
        // joint_command_simple.q_des[RR_2] = -2.4;
        // joint_command_simple.q_des[RL_2] = -2.4;

        
        _firstRun = false;
    }

    // filte the target angles so that they are not too different from the current angles
    // double rate = 
    // for(int i = 0; i < 12; i++){
    //         joint_command_simple.q_des[i] = jointLinearInterpolation(joint_state_simple.q[i],rate)
    // }

    for(int i = 0; i < 12; i++){
        cmd.motorCmd[i].q = joint_command_simple.q_des[i];
        cmd.motorCmd[i].dq = joint_command_simple.qd_des[i];
        cmd.motorCmd[i].Kp = joint_command_simple.kp[i];
        cmd.motorCmd[i].Kd = joint_command_simple.kd[i];
        cmd.motorCmd[i].tau = joint_command_simple.tau_ff[i];
    }

    // Hip Limits
    // cmd.motorCmd[FR_0].q = std::min(std::max(cmd.motorCmd[FR_0].q, (float)go1_Hip_min_mod), (float)go1_Hip_max_mod);
    // cmd.motorCmd[FL_0].q = std::min(std::max(cmd.motorCmd[FL_0].q, (float)go1_Hip_min_mod), (float)go1_Hip_max_mod);
    // cmd.motorCmd[RR_0].q = std::min(std::max(cmd.motorCmd[RR_0].q, (float)go1_Hip_min_mod), (float)go1_Hip_max_mod);
    // cmd.motorCmd[RL_0].q = std::min(std::max(cmd.motorCmd[RL_0].q, (float)go1_Hip_min_mod), (float)go1_Hip_max_mod);

    // // Thigh Limits
    // cmd.motorCmd[FR_1].q = std::min(std::max(cmd.motorCmd[FR_1].q, (float)go1_Thigh_min_mod), (float)go1_Thigh_max_mod);
    // cmd.motorCmd[FL_1].q = std::min(std::max(cmd.motorCmd[FL_1].q, (float)go1_Thigh_min_mod), (float)go1_Thigh_max_mod);
    // cmd.motorCmd[RR_1].q = std::min(std::max(cmd.motorCmd[RR_1].q, (float)go1_Thigh_min_mod), (float)go1_Thigh_max_mod);
    // cmd.motorCmd[RL_1].q = std::min(std::max(cmd.motorCmd[RL_1].q, (float)go1_Thigh_min_mod), (float)go1_Thigh_max_mod);

    // // Calf Limits
    // cmd.motorCmd[FR_2].q = std::min(std::max(cmd.motorCmd[FR_2].q, (float)go1_Calf_min_mod), (float)go1_Calf_max_mod);
    // cmd.motorCmd[FL_2].q = std::min(std::max(cmd.motorCmd[FL_2].q, (float)go1_Calf_min_mod), (float)go1_Calf_max_mod);
    // cmd.motorCmd[RR_2].q = std::min(std::max(cmd.motorCmd[RR_2].q, (float)go1_Calf_min_mod), (float)go1_Calf_max_mod);
    // cmd.motorCmd[RL_2].q = std::min(std::max(cmd.motorCmd[RL_2].q, (float)go1_Calf_min_mod), (float)go1_Calf_max_mod);



    printf("State Angles: %f  %f  %f     ", state.motorState[FR_0].q, state.motorState[FR_1].q, state.motorState[FR_2].q);
    printf("Cmd Angles: %f  %f  %f     ", cmd.motorCmd[FR_0].q, cmd.motorCmd[FR_1].q, cmd.motorCmd[FR_2].q);
    //printf("Cmd Vels: %f  %f  %f     ", cmd.motorCmd[FR_0].dq, cmd.motorCmd[FR_1].dq, cmd.motorCmd[FR_2].dq);
    //printf("Cmd Tau: %f  %f  %f\n", cmd.motorCmd[FR_0].tau, cmd.motorCmd[FR_1].tau, cmd.motorCmd[FR_2].tau);
    printf("Cmd Kp: %f  %f  %f     ", cmd.motorCmd[FR_0].Kp, cmd.motorCmd[FR_1].Kp, cmd.motorCmd[FR_2].Kp);
    printf("Cmd Kd: %f  %f  %f\n", cmd.motorCmd[FR_0].Kd, cmd.motorCmd[FR_1].Kd, cmd.motorCmd[FR_2].Kd);
    //state.motorState[FR_1].dq, state.motorState[FR_1].q, state.motorState[FR_1].dq);


    safe.PositionLimit(cmd);
    int res1 = safe.PowerProtect(cmd, state, 9);
    //int res2 = safe.PositionProtect(cmd, state, 1); //0.087 = 5 degree
    printf("%d\n",res1);
    if(res1 < 0) exit(-1);

    udp.SetSend(cmd);

}


int main(void)
{
    std::cout << "Communication level is set to LOW-level." << std::endl
              << "WARNING: Make sure the robot is hung up." << std::endl
              << "Press Enter to continue..." << std::endl;
    std::cin.ignore();

    Custom custom(LOWLEVEL);
    custom.init();
    
    // InitEnvironment();
    LoopFunc loop_control("control_loop", custom.dt,    boost::bind(&Custom::RobotControl, &custom));
    LoopFunc loop_udpSend("udp_send",     custom.dt, 3, boost::bind(&Custom::UDPSend,      &custom));
    LoopFunc loop_udpRecv("udp_recv",     custom.dt, 3, boost::bind(&Custom::UDPRecv,      &custom));

    loop_udpSend.start();
    loop_udpRecv.start();
    loop_control.start();

    while(1){
        sleep(10);
    };

    return 0;
}
