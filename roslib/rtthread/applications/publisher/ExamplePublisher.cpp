/*
 * File      : ExamplePublisher.cpp
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

int main(void) {
  //{ init lwip
  lwip_sys_init();
  // }

  tinyros::Publisher hello_pub ("tinyros_hello", new tinyros_hello::TinyrosHello());
  tinyros::nh()->advertise(hello_pub);
  while (true) {
    tinyros_hello::TinyrosHello msg;
    msg.hello = "Hello, tiny-ros ^_^ ";
    if (hello_pub.negotiated()) {
      hello_pub.publish (&msg);
    }
    rt_thread_delay(1000);
  }
  return 0;
}
