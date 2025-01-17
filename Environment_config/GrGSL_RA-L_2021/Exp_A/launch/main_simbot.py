"""
	Launch file to run the GADEN-preprocessing node.
	This is mandatory before running GADEN simulator.
	From the CAD models in the selected scenario, it generates a 3D and 2D occupancy gridmap
	that will be employed by GADEN and nav2 to simulate dispersion and navigation. Moreover parses
	the 3D cloud of wind-vectors obtained from CFD, to a grid-based format.
"""
import os

from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription, OpaqueFunction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.actions import DeclareLaunchArgument, SetEnvironmentVariable, SetLaunchConfiguration, GroupAction
from launch.substitutions import LaunchConfiguration
from launch.frontend.parse_substitution import parse_substitution
from launch_ros.actions import Node, PushRosNamespace
from launch.conditions import IfCondition
from ament_index_python.packages import get_package_share_directory

def launch_arguments():
	return [
		# ==================
			DeclareLaunchArgument(
				"launchCoppelia", default_value=["True"],
			),
			DeclareLaunchArgument(
				"scenario",	default_value=["Exp_A"],
			),
			DeclareLaunchArgument(
				"use_infotaxis",	default_value=["False"],
			),
	]

def launch_setup(context, *args, **kwargs):
	
	gsl = [
		GroupAction(actions=[
			PushRosNamespace(LaunchConfiguration("robot_name")),
			Node(
				package="gsl_server",
				executable="gsl_actionserver_call",
				name="gsl_call",
				parameters=[
					{"method": "GrGSL"},
				],
			),
			Node(
				package="gsl_server",
				executable="gsl_actionserver_node",
				name="gsl_node",
				prefix="xterm -hold -e",
				parameters=[
					# Common
					{"robot_location_topic": "ground_truth"},
					{"stop_and_measure_time": 1.0},
					{"th_gas_present": 0.5},
					{"th_wind_present": 0.1},
					{"ground_truth_x": parse_substitution("$(var source_location_x)")},
					{"ground_truth_y": parse_substitution("$(var source_location_y)")},
					{"results_file": parse_substitution("Results/GridGSL_$(var scenario)_Infotaxis_$(var use_infotaxis).csv")},
					{"anemometer_frame": parse_substitution("$(var robot_name)_anemometer_frame")},
					
					#GrGSL
					{"scale": 60},
					{"stdev_hit": 1.0},
					{"stdev_miss": 1.5},
					{"convergence_thr": 0.5},
					{"infoTaxis": parse_substitution("$(var use_infotaxis)")},
					
					#Surge-Cast
					{"step": 0.8},
				],
			),
		])
	]


	coppelia = [
		IncludeLaunchDescription(
			PythonLaunchDescriptionSource(
					os.path.join(
						get_package_share_directory("grgsl_env"),
						"navigation_config/conditional_coppelia_launch.py",
					)
				),
				launch_arguments={
					"launchCoppelia": LaunchConfiguration("launchCoppelia").perform(context),
					"scenePath" : parse_substitution("$(find-pkg-share grgsl_env)/$(var scenario)/coppeliaScene.ttt"),
					"autoplay" : "False",
					"headless" : "True"
				}.items(),
		),
		Node(
			package="gaden_preprocessing",
			executable="configureCoppeliaSim",
			name="configureCoppeliaSim",
			condition=IfCondition(LaunchConfiguration("launchCoppelia")),
			parameters=[
				{"permanentChange" : False},
				{"robotName" : LaunchConfiguration("robot_name")},
				{"position" : [7.3, 5.4, 0.0]},
            ],
		)
	]
	
	nav2 = IncludeLaunchDescription(
			PythonLaunchDescriptionSource(
					os.path.join(
						get_package_share_directory("grgsl_env"),
						"navigation_config/nav2_launch.py",
					)
				),
				launch_arguments={
					"scenario": LaunchConfiguration("scenario"),
					"namespace" : LaunchConfiguration("robot_name")
				}.items(),
		)
	
	nav_assistant = [
		GroupAction(actions=[
			PushRosNamespace(LaunchConfiguration("robot_name")),
			Node(
            package="topology_graph",
            executable="topology_graph_node",
            name="topology_graph",
            parameters=[
                {"verbose": False},
				{"get_plan_server":"compute_path_to_pose"},
            ],
			),
			Node(
				package="navigation_assistant",
				executable="nav_assistant_node",
				name="nav_assistant",
				prefix="xterm -e",
				parameters=[
					{"use_CNP": "False"},
					{"init_from_file": ""},
					{"save_to_file": ""},
					{"localization_topic" : "ground_truth"},
				],
			),
			Node(
				package="nav_assistant_functions",
				executable="nav_assistant_functions_node",
				name="nav_assistant_functions",
				parameters=[
					{"verbose": False},
				],
			)
		]),     
	]

	rviz = Node(
		package="rviz2",
		executable="rviz2",
		name="rviz",
		prefix="xterm -hold -e",
		arguments=[
			"-d" + os.path.join(get_package_share_directory("grgsl_env"), "gaden.rviz")
		],
	)
	
	gaden = [
		Node(
			package="gaden_environment",
			executable="environment",
			name="environment",
			parameters=[
				{"verbose" : True},
				{"wait_preprocessing" : False},
				{"fixed_frame" : "map"},

				{"CAD_0" : parse_substitution("$(find-pkg-share grgsl_env)/$(var scenario)/cad_models/10x6_walls.dae")},
				{"CAD_0_color" : [0.82, 0.86, 0.86]},

				{"CAD_1" : parse_substitution("$(find-pkg-share grgsl_env)/$(var scenario)/cad_models/10x6_central_obstacle.dae")},
				{"CAD_1_color" : [0.82, 0.86, 0.86]},

				{"CAD_2" : parse_substitution("$(find-pkg-share grgsl_env)/$(var scenario)/cad_models/10x6_door_left.dae")},
				{"CAD_2_color" : [0.96, 0.17, 0.3]},

				{"CAD_3" : parse_substitution("$(find-pkg-share grgsl_env)/$(var scenario)/cad_models/10x6_door_right.dae")},
				{"CAD_3_color" : [0.96, 0.17, 0.3]},

				{"occupancy3D_data" : parse_substitution("$(find-pkg-share grgsl_env)/$(var scenario)/OccupancyGrid3D.csv")},
				
				{"number_of_sources" : 1},
				{"source_0_position_x" : parse_substitution("$(var source_location_x)")},
				{"source_0_position_y" : parse_substitution("$(var source_location_y)")},
				{"source_0_position_z" : parse_substitution("$(var source_location_z)")},

				{"source_0_scale" : 0.2},
				{"source_0_color" : [0.0, 1.0, 0.0]},
            ],
		),
		Node(
			package="gaden_player",
			executable="player",
			name="gaden_player",
			parameters=[
				{"verbose" : False},
				{"player_freq" : 2.0},
				{"initial_iteration" : 35},
				{"num_simulators" : 1},

				{"simulation_data_0" : parse_substitution("$(find-pkg-share grgsl_env)/$(var scenario)/gas_simulations/sim1")},
				{"occupancyFile" : parse_substitution("$(find-pkg-share grgsl_env)/$(var scenario)/OccupancyGrid3D.csv")},

				{"allow_looping" : True},
				{"loop_from_iteration" : 40},
				{"loop_to_iteration" : 45},
			]
		),
	]

	anemometer = [
		GroupAction(actions=[
			PushRosNamespace(LaunchConfiguration("robot_name")),
			Node(
				package="simulated_anemometer",
				executable="simulated_anemometer",
				name="Anemometer",
				parameters=[
					{"sensor_frame" : parse_substitution("$(var robot_name)_anemometer_frame") },
					{"fixed_frame" : "map"},
					{"noise_std" : 0.3},
					{"use_map_ref_system" : False},
					{'use_sim_time': True},
				]
			),
			Node(
				package='tf2_ros',
				executable='static_transform_publisher',
				name='anemometer_tf_pub',
				arguments = ['0', '0', '0.5', '1.0', '0.0', '0', '0', parse_substitution('$(var robot_name)_base_link'), parse_substitution('$(var robot_name)_anemometer_frame')],
				parameters=[{'use_sim_time': True}]
			),
		])
	]

	PID = [
		GroupAction(actions=[
			PushRosNamespace(LaunchConfiguration("robot_name")),
			Node(
				package="simulated_gas_sensor",
				executable="simulated_gas_sensor",
				name="PID",
				parameters=[
					{"sensor_model" : 30 },
					{"sensor_frame" : parse_substitution("$(var robot_name)_pid_frame") },
					{"fixed_frame" : "map"},
					{"noise_std" : 20.1},
					{'use_sim_time': True},
				]
			),
			Node(
				package='tf2_ros',
				executable='static_transform_publisher',
				name='pid_tf_pub',
				arguments = ['0', '0', '0.5', '1.0', '0.0', '0', '0', parse_substitution('$(var robot_name)_base_link'), parse_substitution('$(var robot_name)_pid_frame')],
				parameters=[{'use_sim_time': True}]
			),
		])
	]

	gmrf_wind = Node(
		package="gmrf_wind_mapping",
		executable="gmrf_wind_mapping_node",
		name="gmrf",
		parameters=[
			{"sensor_topic": parse_substitution("$(var robot_name)/Anemometer/WindSensor_reading")},
			{"map_topic": parse_substitution("$(var robot_name)/map")},
			{"cell_size": 0.6},
		]
	)


	returnList = []
	returnList.extend(coppelia)
	returnList.extend(gaden)
	returnList.append(nav2)
	returnList.extend(nav_assistant)
	returnList.extend(anemometer)
	returnList.extend(PID)
	returnList.append(gmrf_wind)
	returnList.append(rviz)
	returnList.extend(gsl)

	return returnList



def generate_launch_description():

    launch_description = [
        # Set env var to print messages to stdout immediately
        SetEnvironmentVariable("RCUTILS_LOGGING_BUFFERED_STREAM", "1"),
        SetEnvironmentVariable("RCUTILS_COLORIZED_OUTPUT", "1"),
        
		SetLaunchConfiguration(
            name="source_location_x",
            value=["2.00"],
        ),
		SetLaunchConfiguration(
            name="source_location_y",
            value=["3.00"],
        ),
		SetLaunchConfiguration(
            name="source_location_z",
            value=["0.50"],
        ),
		SetLaunchConfiguration(
            name="robot_name",
            value=["PioneerP3DX"],
        ),
    ]
    
    launch_description.extend(launch_arguments())
    launch_description.append(OpaqueFunction(function=launch_setup))
    
    return  LaunchDescription(launch_description)