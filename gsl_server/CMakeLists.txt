cmake_minimum_required(VERSION 3.11)
project(gsl_server)
set(CMAKE_BUILD_TYPE "None") 
set(CMAKE_CXX_FLAGS "-std=c++20 -g ${CMAKE_CXX_FLAGS}")



#OPTIONS
##############################################################################
set(GSL_DEBUG OFF) #enabling debug mode disables optimizations and activates a bunch of assertions in the code.
set(GSL_TRACING ON) #extra logging to tell you what is being executed

set(USE_GADEN ON) #if ON, the code is compiled with support for directly querying gaden simulations to obtain the wind ground truth. If you dont have gaden in this workspace, you need to set this to OFF or it wont compile (duh)

set(USE_NAV_ASSISTANT OFF) # if ON uses the nav_assistant package rather than directly communicating with nav_2. It adds some extra features, but currently it has a relatively high performance cost due to async inter-process communication  

# Compile graphical interface with DearImGUI. Currently only affects the PMFS algorithm. Not required to function, it is mostly for debugging purposes
set(USE_GUI ON)

##############################################################################




 












if(GSL_DEBUG)
  add_definitions(-DGSL_DEBUG=1)
  set(CMAKE_CXX_FLAGS "-O0 ${CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_CXX_FLAGS "-O3 ${CMAKE_CXX_FLAGS}")
endif(GSL_DEBUG)



#ament packages
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(angles REQUIRED)
find_package(olfaction_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(gmrf_wind_mapping REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(gsl_actions REQUIRED)


if(GSL_TRACING)
    add_definitions(-DGSL_TRACING=1)
endif(GSL_TRACING)

if(USE_GADEN)
    add_definitions(-DUSE_GADEN)
    find_package(gaden_player REQUIRED)
    set(CONDITIONAL_GADEN_PLAYER "gaden_player")  
else()
    set(CONDITIONAL_GADEN_PLAYER "")  
endif(USE_GADEN)


if(USE_NAV_ASSISTANT)
    add_definitions(-DUSE_NAV_ASSISTANT)
	find_package(nav_assistant_msgs REQUIRED)
    set(CONDITIONAL_NAV_ASSISTANT "nav_assistant_msgs")  
endif()


#other stuff
find_package(OpenMP REQUIRED)
find_package(OpenCV REQUIRED)
find_package(fmt REQUIRED)

if(USE_GUI)
	add_definitions(-DUSE_GUI)
    find_package(ament_imgui REQUIRED)
    set(CONDITIONAL_IMGUI "ament_imgui")
endif(USE_GUI)



###########
## INCLUDES
###########

include_directories(
  include
  third_party/glm
  third_party/StateMachine/include
)

###########
## SOURCES
###########

file(GLOB_RECURSE SOURCES_SERVER "src/server/*.cpp")


###########
#DECLARE EXECUTABLES
###########

add_executable(gsl_actionserver_node ${SOURCES_SERVER})
add_executable(gsl_actionserver_call src/gsl_server_call.cpp)

ament_target_dependencies( gsl_actionserver_node
    rclcpp
    rclcpp_action
    
    geometry_msgs
    tf2
    tf2_ros
    tf2_geometry_msgs
    angles
    
    olfaction_msgs
    visualization_msgs
    nav2_msgs
    std_msgs
	
    gmrf_wind_mapping
    gsl_actions
	
    ${CONDITIONAL_NAV_ASSISTANT}
    ${CONDITIONAL_GADEN_PLAYER}
    ${CONDITIONAL_IMGUI}
)

ament_target_dependencies( gsl_actionserver_call
    rclcpp
    rclcpp_action
    gsl_actions
)

###########
#LINK
###########
target_link_libraries(gsl_actionserver_node  
  ${OpenCV_LIBS}
  OpenMP::OpenMP_CXX
  fmt
)


install(
  TARGETS gsl_actionserver_node  gsl_actionserver_call
  DESTINATION lib/${PROJECT_NAME}
)

install(
	DIRECTORY resources
	DESTINATION share/${PROJECT_NAME}
)
ament_package()