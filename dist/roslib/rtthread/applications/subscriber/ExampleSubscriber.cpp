/*
 * File      : ExampleService.cpp
 * This file is part of tinyros
 * Copyright (C) UBTECH Robotics 2018
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-01-22     Pinkie.Fu    initial version
 */
#include <rtthread.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <netif/ethernetif.h>
#include "tiny_ros/ros.h"
#include "tiny_ros/tinyros_hello/TinyrosHello.h"

extern "C" void lwip_sys_init(void);

static void subscriber_cb(const tinyros_hello::TinyrosHello& received_msg) {
  printf("%s\n", received_msg.hello.c_str());
}

int main(void) {
  //{ init lwip
  lwip_sys_init();
  // }

  tinyros::Subscriber<tinyros_hello::TinyrosHello> sub("tinyros_hello", subscriber_cb);
  tinyros::nh()->subscribe(sub);
  while(true) {
    rt_thread_delay(10*1000);
  }

  return 0;
}
