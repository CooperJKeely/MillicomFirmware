cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(firmware)

target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_MILLIMOBILE_MODE_SWITCHING app PRIVATE src/mode.c)
target_sources_ifdef(CONFIG_MILLIMOBILE_ADV app PRIVATE src/adv.c)
target_sources_ifdef(CONFIG_MILLIMOBILE_SYNC app PRIVATE src/sync.c)