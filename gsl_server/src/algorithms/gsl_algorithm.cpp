#include <algorithms/gsl_algorithm.h>

using namespace std::chrono_literals;
using namespace std::placeholders;

GSLAlgorithm::GSLAlgorithm(std::shared_ptr<rclcpp::Node> _node)
{
    node = _node;
}

void GSLAlgorithm::initialize()
{
    // Navigation
    //-----------

#ifdef USE_NAV_ASSISTANT
    make_plan_client = node->create_client<MakePlan>("navigation_assistant/make_plan");
    nav_client = rclcpp_action::create_client<NavigateToPose>(node, "nav_assistant");
#else
    make_plan_client = rclcpp_action::create_client<MakePlan>(node, "compute_path_to_pose");
    nav_client = rclcpp_action::create_client<NavigateToPose>(node, "navigate_to_pose");
#endif

    spdlog::info("[GSL_NODE] Waiting for the move_base action server to come online...");
    bool mb_aconline = false;

    for (int i = 0; i < 100; i++)
    {
        if (nav_client->wait_for_action_server(1s))
        {
            mb_aconline = true;
            break;
        }
        spdlog::info("[GSL_NODE] Unable to find the move_base action server, retrying...");
    }

    if (!mb_aconline)
    {
        spdlog::error("[GSL_NODE] No move_base node found. Please ensure the move_base node is active.");
        return;
    }
    spdlog::info("[GSL_NODE] Found MoveBase! Initializing module...");

    //-----------
    declareParameters();

    // Subscribers
    //------------
    gas_sub_ = node->create_subscription<olfaction_msgs::msg::GasSensor>(enose_topic, 1, std::bind(&GSLAlgorithm::gasCallback, this, _1));
    wind_sub_ = node->create_subscription<olfaction_msgs::msg::Anemometer>(anemometer_topic, 1, std::bind(&GSLAlgorithm::windCallback, this, _1));
    map_sub_ = node->create_subscription<nav_msgs::msg::OccupancyGrid>(map_topic, rclcpp::QoS(1).reliable().transient_local(),
                                                                       std::bind(&GSLAlgorithm::mapCallback, this, _1));
    costmap_sub_ = node->create_subscription<nav_msgs::msg::OccupancyGrid>(costmap_topic, 1, std::bind(&GSLAlgorithm::costmapCallback, this, _1));
    localization_sub_ =
        node->create_subscription<PoseWithCovarianceStamped>(robot_location_topic, 100, std::bind(&GSLAlgorithm::localizationCallback, this, _1));

    // Init State
    spdlog::info("[GSL NODE] INITIALIZATON COMPLETED--> WAITING_FOR_MAP");
    inMotion = false;
    inExecution = false;

    tf_buffer = std::make_unique<tf2_ros::Buffer>(node->get_clock());
    tf_listener = std::make_shared<tf2_ros::TransformListener>(*tf_buffer);
}

void GSLAlgorithm::declareParameters()
{

    enose_topic = getParam<std::string>("enose_topic", "PID/Sensor_reading");
    anemometer_topic = getParam<std::string>("anemometer_topic", "Anemometer/WindSensor_reading");
    robot_location_topic = getParam<std::string>("robot_location_topic", "amcl_pose");
    map_topic = getParam<std::string>("map_topic", "map");
    costmap_topic = getParam<std::string>("costmap_topic", "global_costmap/costmap");

    max_search_time = getParam<double>("max_search_time", 1000.0);
    distance_found = getParam<double>("distance_found", 0.5);
    source_pose_x = getParam<double>("ground_truth_x", 0.0);
    source_pose_y = getParam<double>("ground_truth_y", 0.0);
    verbose = getParam<bool>("verbose", false);

    results_file = getParam<std::string>("results_file", "");
    errors_file = getParam<std::string>("errors_file", "");
    path_file = getParam<std::string>("path_file", "");
}

GSLAlgorithm::~GSLAlgorithm()
{
}

//-----------------------------

// CALLBACKS

//-----------------------------

void GSLAlgorithm::localizationCallback(const PoseWithCovarianceStamped::SharedPtr msg)
{
    // keep the most recent robot pose
    current_robot_pose = *msg;

    // Keep all poses for later distance estimation
    robot_poses_vector.push_back(current_robot_pose);
}

void GSLAlgorithm::costmapCallback(const nav_msgs::msg::OccupancyGrid::SharedPtr msg)
{
    costmap_ = *msg;
}

// Move Base CallBacks
void GSLAlgorithm::goalDoneCallback(const rclcpp_action::ClientGoalHandle<NavigateToPose>::WrappedResult& result)
{
    if (result.code != rclcpp_action::ResultCode::SUCCEEDED)
        spdlog::error("PlumeTracking - OOPS! Couldn't reach the target. Navigation goal ReturnCode: {}", (int)result.code);

    // Notify that the objective has been reached
    inMotion = false;
}

//-----------------

// AUX

//-----------------

bool GSLAlgorithm::get_inMotion()
{
    return inMotion;
}

float GSLAlgorithm::get_average_vector(std::vector<float> const& v)
{
    size_t length = v.size();
    float sum = 0.0;
    for (std::vector<float>::const_iterator i = v.begin(); i != v.end(); ++i)
        sum += *i;

    return sum / length;
}

bool GSLAlgorithm::checkGoal(const NavigateToPose::Goal& goal)
{
#ifdef USE_NAV_ASSISTANT
    // spdlog::info("[DEBUG] Checking Goal [{:.2}, {:.2}] in map frame", goal.pose.pose.position.x, goal.pose.pose.position.y);
    float pos_x = goal.pose.pose.position.x;
    float pos_y = goal.pose.pose.position.y;
    if (!isPointInsideMapBounds({pos_x, pos_y}))
    {
        if (verbose)
            spdlog::warn("Goal [{:.2}, {:.2}] is out of map dimensions", pos_x, pos_y);
        return false;
    }

    auto request = std::make_shared<MakePlan::Request>();
    request->start.header.frame_id = "map";
    request->start.header.stamp = node->now();
    request->start.pose = current_robot_pose.pose.pose;

    request->goal = goal.pose;

    auto future = make_plan_client->async_send_request(request);
    if (rclcpp::spin_until_future_complete(node, future, std::chrono::seconds(1)) != rclcpp::FutureReturnCode::SUCCESS)
    {
        spdlog::error("Unable to call make_plan!");
        return false;
    }

    auto response = future.get();
    if (response->plan.poses.empty())
        return false;
    else
        return true;
#else
    MakePlan::Goal mb_request;

    mb_request.use_start = true;
    mb_request.start.pose = current_robot_pose.pose.pose;
    mb_request.start.header = current_robot_pose.header;
    mb_request.goal = goal.pose;

    // send the "make plan" goal to nav2 and wait until the response comes back
    std::optional<nav_msgs::msg::Path> m_CurrentPlan = std::nullopt;

    auto callback = [&m_CurrentPlan, this](const rclcpp_action::ClientGoalHandle<MakePlan>::WrappedResult& w_result)
    {
        m_CurrentPlan = w_result.result->path;
        // spdlog::info("Path calculated");
    };
    auto goal_options = rclcpp_action::Client<MakePlan>::SendGoalOptions();
    goal_options.result_callback = callback;

    auto future = make_plan_client->async_send_goal(mb_request, goal_options);
    auto result = rclcpp::spin_until_future_complete(node, future);

    // Check if valid goal with move_base srv
    if (result != rclcpp::FutureReturnCode::SUCCESS)
    {
        // SRV is not available!! Report Error
        spdlog::info("[] Unable to call MAKE_PLAN service from MoveBase");
        return false;
    }

    // wait until path is received. The spin-until-future-complete line only blocks until the request is accepted!
    rclcpp::Rate wait_rate(200);
    while (!m_CurrentPlan.has_value())
    {
        wait_rate.sleep();
        rclcpp::spin_some(node);
    }

    if (m_CurrentPlan.value().poses.empty())
    {
        if (verbose)
            spdlog::warn("[NavigateToPose] Unable to get plan");
        return true;
    }
    return true;
#endif
}

void GSLAlgorithm::sendGoal(const NavigateToPose::Goal& goal)
{
    rclcpp_action::Client<NavigateToPose>::SendGoalOptions options;
    options.result_callback = std::bind(&GSLAlgorithm::goalDoneCallback, this, _1);
    auto future = nav_client->async_send_goal(goal, options);
    rclcpp::FutureReturnCode code = rclcpp::spin_until_future_complete(node, future);
    if (code != rclcpp::FutureReturnCode::SUCCESS)
    {
        spdlog::error("Error sending goal to navigation server! Received code {}", code);
        rclcpp_action::ClientGoalHandle<NavigateToPose>::WrappedResult result;
        result.code = rclcpp_action::ResultCode::ABORTED;
        goalDoneCallback(result);
    }
}

int GSLAlgorithm::checkSourceFound()
{
    if (inExecution)
    {
        // 1. Check that working time < max allowed time for search
        rclcpp::Duration time_spent = node->now() - start_time;
        if (time_spent.seconds() > max_search_time)
        {
            // Report failure, we were too slow
            spdlog::info("FAILURE-> Time spent ({} s) > max_search_time = {}", time_spent.seconds(), max_search_time);
            save_results_to_file(0);
            return 0;
        }

        // 2. Distance from robot to source
        double Ax = current_robot_pose.pose.pose.position.x - source_pose_x;
        double Ay = current_robot_pose.pose.pose.position.y - source_pose_y;
        double dist = sqrt(pow(Ax, 2) + pow(Ay, 2));
        if (dist < distance_found)
        {
            // GSL has finished with success!
            spdlog::info("SUCCESS -> Time spent ({} s)", time_spent.seconds());
            save_results_to_file(1);
            return 1;
        }
    }

    // In other case, we are still searching (keep going)
    return -1;
}

void GSLAlgorithm::save_results_to_file(int result)
{
    nav_client->async_cancel_all_goals();

    // 1. Search time.
    rclcpp::Duration time_spent = node->now() - start_time;
    double search_t = time_spent.seconds();

    // 2. Search distance
    // Get distance from array of path followed (vector of PoseWithCovarianceStamped
    double search_d;
    double Ax, Ay, d = 0;

    for (size_t h = 1; h < robot_poses_vector.size(); h++)
    {
        Ax = robot_poses_vector[h - 1].pose.pose.position.x - robot_poses_vector[h].pose.pose.position.x;
        Ay = robot_poses_vector[h - 1].pose.pose.position.y - robot_poses_vector[h].pose.pose.position.y;
        d += sqrt(pow(Ax, 2) + pow(Ay, 2));
    }
    search_d = d;

    // 3. Navigation distance (from robot to source)
    // Estimate the distances by getting a navigation path from Robot initial pose to Source points in the map
    double nav_d;
    geometry_msgs::msg::PoseStamped source_pose;
    source_pose.header.frame_id = "map";
    source_pose.header.stamp = node->now();
    source_pose.pose.position.x = source_pose_x;
    source_pose.pose.position.y = source_pose_y;

    // Set MoveBase srv to estimate the distances

    // 4. Nav time
    double nav_t = nav_d / 0.4; // assumming a cte speed of 0.4m/s
    std::string str = fmt::format("RESULT IS: Success={}, Search_d={}, Nav_d={}, Search_t={}, Nav_t={}\n", result, search_d, nav_d, search_t, nav_t);
    spdlog::info(str.c_str());

    // Save to file

    if (FILE* output_file = fopen(results_file.c_str(), "w"))
    {
        fprintf(output_file, "%s", str.c_str());
        for (PoseWithCovarianceStamped p : robot_poses_vector)
        {
            fprintf(output_file, "%f, %f\n", p.pose.pose.position.x, p.pose.pose.position.y);
        }
        fclose(output_file);
    }
    else
        spdlog::error("Unable to open Results file at: {}", results_file.c_str());
}

bool GSLAlgorithm::isPointInsideMapBounds(const Utils::Vector2& point) const
{
    const static Utils::Vector2 mapStart = (Utils::Vector2)map_.info.origin.position;
    const static Utils::Vector2 mapEnd = mapStart + Utils::Vector2(map_.info.width, map_.info.height) * map_.info.resolution;

    return point.x >= mapStart.x && point.x < mapEnd.x && point.y >= mapStart.y && point.y < mapEnd.y;
}
