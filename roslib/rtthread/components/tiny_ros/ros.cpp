/*
 * File      : ros.cpp
 * This file is part of tiny_ros
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-04-24     Pinkie.Fu    initial version
 */
#include "tiny_ros/ros/node_handle.h"
namespace tinyros {
static std::string ip_addr_ = "127.0.0.1";
  
//////////////////////////////////////////
static bool main_loop_init_ = false;
static void tinyros_main_loop(void* param);
static rt_mutex_t main_loop_mutex_ = rt_mutex_create("ml", RT_IPC_FLAG_FIFO);
static rt_thread_t main_loop_thread_ = rt_thread_create("ml", tinyros_main_loop, RT_NULL, 1024, 21, 20);
static void tinyros_main_loop(void* param) {
retry:
  tinyros::nh()->initNode(ip_addr_);
  while (tinyros::nh()->ok()) {
    tinyros::nh()->spin();
    rt_thread_delay(1000);
  }
  rt_thread_delay(1000);
  goto retry;
}

NodeHandle* nh(){
  static NodeHandle g_nh;
  if (!main_loop_init_){
    rt_mutex_take(main_loop_mutex_, RT_WAITING_FOREVER);
    if (!main_loop_init_) {
      if (rt_thread_startup(main_loop_thread_) == RT_EOK) {
          main_loop_init_ = true;
      }
    }
    rt_mutex_release(main_loop_mutex_);
  }
  return &g_nh;
}
//////////////////////////////////////////
}

