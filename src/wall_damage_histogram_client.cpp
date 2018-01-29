// Std Stuff
#include <iostream>
#include <fstream>
// PCL Stuff
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl_conversions/pcl_conversions.h>
// Ros Stuff
#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>
// Wall_Features
#include "wall_features/wall_damage_estimation.h"
#include "wall_features/wall_damage_estimation.hpp"
#include "wall_features/point_wall_damage.h"
#include "wall_features/wall_damage_histogram.h"
// Primitive_Search
#include <pointcloud_primitive_search/primitive_process.h>
#include <pointcloud_primitive_search/primitive_process_creation.h>
#include <pointcloud_primitive_search/primitive_process_publisher.h>

int main (int argc, char **argv)
{ 
  // --------------------------------------------- Basic ROS Stuffs ---------------------------------------------
  ros::init(argc, argv, "wall_features_client");
  pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS); 

  //if( ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Debug) )
  //  ros::console::notifyLoggerLevelsChanged();  

  ros::NodeHandle nh;

  // --------------------------------------------- Initializing Things ---------------------------------------------

  // Publishers for Clouds
  ros::Publisher input_pub = nh.advertise<sensor_msgs::PointCloud2>("wall_features/input_cloud", 1);
  ros::Publisher wall_pub = nh.advertise<sensor_msgs::PointCloud2>("wall_features/wall_cloud", 1);
  ros::Publisher voxelized_pub = nh.advertise<sensor_msgs::PointCloud2>("wall_features/voxelized_cloud", 1);
  ros::Publisher damage_pub = nh.advertise<sensor_msgs::PointCloud2>("wall_features/damage_cloud", 1);
  ros::Publisher histogram_pub = nh.advertise<sensor_msgs::PointCloud2>("wall_features/histogram_cloud", 1);

  // ROS Msg Clouds
  sensor_msgs::PointCloud2 input_msg;
  sensor_msgs::PointCloud2 wall_msg;
  sensor_msgs::PointCloud2 voxelized_msg;
  sensor_msgs::PointCloud2 damage_msg;
  sensor_msgs::PointCloud2 histogram_msg;

  // PCL Clouds
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr input_cloud (new pcl::PointCloud<pcl::PointXYZRGB>);
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr wall_cloud (new pcl::PointCloud<pcl::PointXYZRGB>);
  pcl::PointCloud<pcl::PointWallDamage>::Ptr wall_damage_cloud (new pcl::PointCloud<pcl::PointWallDamage>);
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr voxelized_cloud (new pcl::PointCloud<pcl::PointXYZRGB>);
  pcl::PointCloud<pcl::WallDamageHistogram>::Ptr histogram_cloud (new pcl::PointCloud<pcl::WallDamageHistogram>);

  // Feature Estimators 
  pcl::WallDamagePointwiseEstimation<pcl::PointXYZRGB, pcl::PointWallDamage> point_damage_estimator;
  pcl::WallDamageHistogramEstimation<pcl::PointWallDamage, pcl::PointXYZRGB, pcl::WallDamageHistogram> damage_histogram_estimator;

  // Primitive Search Stuff
  pointcloud_primitive_search::primitive_process wall_process;
  PrimitiveProcessCreation::createProcesses(&wall_process, "primitive_search");
  PrimitiveProcessPublisher primitive_pub(nh, wall_process);
  ros::ServiceClient wall_finder = nh.serviceClient<pointcloud_primitive_search::primitive_process>("primitive_search");

  // --------------------------------------------- Bag Stuff ---------------------------------------------
  
  bool input_from_bag = false;     // if not from bag, creates a new custom cloud
  nh.param<bool>("wall_features/input_from_bag", input_from_bag, false);

  if(input_from_bag)
  {
    std::string bag_topic = "/laser_stitcher/output_cloud";
    std::string bag_name = "stitched_pointcloud.bag";
    nh.getParam("wall_features/bag_topic", bag_topic);
    nh.getParam("wall_features/bag_name", bag_name);
    ROS_INFO_STREAM("[RegistrationClient] Loading clouds from bag files, using bag name: " << bag_name << " and topic name: " << bag_topic << ".");
    rosbag::Bag input_bag; 
    input_bag.open(bag_name, rosbag::bagmode::Read);

    std::vector<std::string> topics;
    topics.push_back(bag_topic);
    rosbag::View view_1(input_bag, rosbag::TopicQuery(topics));

    BOOST_FOREACH(rosbag::MessageInstance const m, view_1)
    {
        sensor_msgs::PointCloud2::ConstPtr cloud_ptr = m.instantiate<sensor_msgs::PointCloud2>();
        if (cloud_ptr != NULL)
            input_msg = *cloud_ptr;
        else
          ROS_ERROR_STREAM("[RegistrationClient] Cloud caught for first cloud is null...");
    }
    input_bag.close();

    pcl::fromROSMsg(input_msg, *input_cloud);     // the pcl cloud for the input is not actually used anywhere though, when bags are used (instead of a custom input) 
    ROS_INFO_STREAM("[WallFeatures] Input cloud loaded from bag of size " << input_msg.height*input_msg.width);  
  }
  else
  {
    ROS_INFO_STREAM("[WallFeatures] Creating input cloud from specified values.");
    float min_x, max_x, min_y,  max_y, min_z, max_z; 
    float x_range, y_range, z_range;
    nh.param<float>("wall_features/min_x", min_x, 0.0);
    nh.param<float>("wall_features/max_x", max_x, 1.0);
    nh.param<float>("wall_features/min_y", min_y, 0.0);
    nh.param<float>("wall_features/max_y", max_y, 1.0);
    nh.param<float>("wall_features/min_z", min_z, 0.0);
    nh.param<float>("wall_features/max_z", max_z, 1.0);
    nh.param<float>("wall_features/x_range", x_range, 0.01);
    nh.param<float>("wall_features/y_range", y_range, 0.01);
    nh.param<float>("wall_features/z_range", z_range, 0.01);
    float points_per_dim = 30;
    for(int i=0; i<points_per_dim; i++)
      for(int j=0; j<points_per_dim; j++)
        //for(int k=0; k<points_per_dim; k++)
        {
          pcl::PointXYZRGB point;
          point.x = i*(max_x-min_x)/points_per_dim + min_x + (rand()%100 - 50)*(x_range)/100;
          point.y = j*(max_y-min_y)/points_per_dim + min_y + (rand()%100 - 50)*(y_range)/100;//-pow(j,2)/points_per_dim*(max_y-min_y)/points_per_dim + min_y + (rand()%100 - 50)*(y_range)/100;
          point.z = j*(max_z-min_z)/points_per_dim + min_z + (rand()%100 - 50)*(z_range)/100;
          input_cloud->points.push_back(point);
          ROS_INFO_STREAM_THROTTLE(.0001, point.x << " " << point.y << " " << point.z);
        }
    for(int i=0; i<points_per_dim; i++)
      for(int j=0; j<points_per_dim; j++)
        //for(int k=0; k<points_per_dim; k++)
        {
          pcl::PointXYZRGB point;
          point.x = i*(max_x-min_x)/points_per_dim + min_x + (rand()%100 - 50)*(x_range)/100;
          point.y = -pow(j,2)/points_per_dim*(max_y-min_y)/points_per_dim + min_y + (rand()%100 - 50)*(y_range)/100;
          point.z = -j*(max_z-min_z)/points_per_dim + min_z + (rand()%100 - 50)*(z_range)/100;
          input_cloud->points.push_back(point);
          ROS_INFO_STREAM_THROTTLE(.0001, point.x << " " << point.y << " " << point.z);
        } 
    pcl::toROSMsg(*input_cloud, input_msg);
  }

  ROS_INFO_STREAM("[WallFeatures] Created or loaded input cloud with size " << input_msg.height*input_msg.width << " " << input_pub.getTopic());
  input_msg.header.frame_id = "map";
  input_pub.publish(input_msg);

  // --------------------------------------------- Find Wall Within Cloud ---------------------------------------------
  wall_process.request.pointcloud = input_msg;
  int process_size;
  if(!wall_finder.call(wall_process))
    ROS_ERROR_STREAM("[WallFeatures] Failed to perform Primitive_Search on input cloud. Continuing with whole cloud...");
  else
  {
    process_size = wall_process.response.outputs.size();
    ROS_INFO_STREAM("size: " << process_size);
    wall_msg = wall_process.response.outputs[process_size-1].task_results[1].task_pointcloud;
    ROS_INFO_STREAM("[WallFeatures] Successfully called PrimitiveSearch on input cloud. Output wall cloud size: " << wall_msg.height*wall_msg.width);
  }
  pcl::fromROSMsg(wall_msg, *wall_cloud);

  // --------------------------------------------- Wall Damage Estimation ---------------------------------------------
  float wall_coeffs[4];
  for(int i=0; i<wall_process.request.inputs[0].expected_coefficients.size(); i++)
    ROS_ERROR_STREAM("params: " << wall_process.request.inputs[0].expected_coefficients[i]);
  for(int i=0; i<4; i++)
  {
    wall_coeffs[i] = wall_process.request.inputs[0].expected_coefficients[i];
    ROS_ERROR_STREAM("wall coeffs: " << i << " -> "  << wall_coeffs[i]);
  }
  point_damage_estimator.setWallCoefficients(wall_coeffs);
  int k_search;
  nh.param<int>("wall_features/k_search_histogram", k_search, 30);
  point_damage_estimator.setKSearch(k_search);
  point_damage_estimator.compute(*wall_cloud, *wall_damage_cloud);
  ROS_INFO_STREAM("[WallFeatures] Performed pointwise damage estimation - output cloud size is " << wall_damage_cloud->points.size());

  // --------------------------------------------- Voxelization ---------------------------------------------
  pcl::VoxelGrid<pcl::PointXYZRGB> vg;
  *voxelized_cloud = *wall_cloud;
  vg.setInputCloud(voxelized_cloud);
  float leaf_size;
  nh.param<float>("wall_features/leaf_size", leaf_size, 0.01);
  ROS_ERROR_STREAM("leaf size: " << leaf_size);
  vg.setLeafSize(leaf_size, leaf_size, leaf_size);
  pcl::PointCloud<pcl::PointXYZRGB> temp_pc;
  vg.filter(temp_pc);
  *voxelized_cloud = temp_pc; 
  ROS_INFO_STREAM("[WallFeatures] Performed voxelization of input cloud - output cloud size is " << voxelized_cloud->points.size());

  float lower_angle_bin_limit, upper_angle_bin_limit, lower_dist_bin_limit, upper_dist_bin_limit;
  bool automatically_set_bins;
  nh.param<bool>("wall_features/automatically_set_bins", automatically_set_bins, true);
  if(automatically_set_bins)
  {
    lower_angle_bin_limit = wall_damage_cloud->points[0].angle_offset;
    upper_angle_bin_limit = lower_angle_bin_limit;
    lower_dist_bin_limit = wall_damage_cloud->points[0].dist_offset;
    upper_dist_bin_limit = lower_dist_bin_limit;
    for(int i=0; i<wall_damage_cloud->size(); i++)
    {
      if(wall_damage_cloud->points[i].angle_offset < lower_angle_bin_limit)
        lower_angle_bin_limit = wall_damage_cloud->points[i].angle_offset;
      if(wall_damage_cloud->points[i].angle_offset > upper_angle_bin_limit)
        upper_angle_bin_limit = wall_damage_cloud->points[i].angle_offset;
      if(wall_damage_cloud->points[i].dist_offset < lower_dist_bin_limit)
        lower_dist_bin_limit = wall_damage_cloud->points[i].dist_offset;
      if(wall_damage_cloud->points[i].dist_offset > upper_dist_bin_limit)
        upper_dist_bin_limit = wall_damage_cloud->points[i].dist_offset;
    }
  }
  else
  {
    nh.param<float>("wall_features/lower_angle_bin_limit", lower_angle_bin_limit, 0);
    nh.param<float>("wall_features/upper_angle_bin_limit", upper_angle_bin_limit, 3.14159);
    nh.param<float>("wall_features/lower_dist_bin_limit", lower_dist_bin_limit, -0.02);
    nh.param<float>("wall_features/upper_dist_bin_limit", upper_dist_bin_limit, 0.02);
  }
  ROS_ERROR_STREAM(lower_angle_bin_limit << " " << upper_angle_bin_limit << " " << lower_dist_bin_limit << " " << upper_dist_bin_limit);
  damage_histogram_estimator.setBinLimits(lower_angle_bin_limit, upper_angle_bin_limit, lower_dist_bin_limit, upper_dist_bin_limit);
  nh.param<int>("wall_features/k_search_histogram", k_search, 30);
  damage_histogram_estimator.setKSearch(k_search);
  damage_histogram_estimator.compute(*wall_damage_cloud, *voxelized_cloud, *histogram_cloud);
  ROS_INFO_STREAM("[WallFeatures] Performed histogram cloud estimation.");

  std::ofstream output_file;
  output_file.open ("histogram_point.csv");
  output_file << "Point 1\n";
  int histogram_size = 80;
  for(int i=0; i<histogram_cloud->size(); i++)
  {
    for(int j=0; j<histogram_size-1; j++)
      output_file << histogram_cloud->points[i].histogram[j] << ", ";
    output_file << histogram_cloud->points[i].histogram[histogram_size-1] << "\n";
  }
  output_file.close();

  pcl::toROSMsg(*voxelized_cloud, voxelized_msg);
  pcl::toROSMsg(*wall_damage_cloud, damage_msg);
  pcl::toROSMsg(*histogram_cloud, histogram_msg);

  voxelized_msg.header.frame_id = "map";
  damage_msg.header.frame_id = "map";
  histogram_msg.header.frame_id = "map";

  while(ros::ok())
  {
    input_pub.publish(input_msg);
    wall_pub.publish(wall_msg);
    voxelized_pub.publish(voxelized_msg);
    damage_pub.publish(damage_msg);
    histogram_pub.publish(histogram_msg);
    primitive_pub.publish(wall_process);
    ros::Duration(1.0).sleep();
  }

  return 0;
}
