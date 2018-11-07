#include <math.h>

#include "ros/ros.h"

#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <sensor_msgs/LaserScan.h>
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include <std_msgs/Int32MultiArray.h>
#include <std_msgs/MultiArrayLayout.h>
#include <std_msgs/MultiArrayDimension.h>
#include <std_msgs/Time.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>

#define PI 3.14159265358979323846

//Motor
int64_t motor_vel_0 = 0;
int64_t motor_vel_1 = 0;
int64_t motor_vel_2 = 0;
int64_t motor_vel_3 = 0;
//LiDAR
sensor_msgs::LaserScan scan_ori;

void scanCallback(const sensor_msgs::LaserScan::ConstPtr& scan)
{
	scan_ori.header.frame_id = scan->header.frame_id;
	scan_ori.angle_min = scan->angle_min;
	scan_ori.angle_max = scan->angle_max;
	scan_ori.angle_increment = scan->angle_increment;
	scan_ori.scan_time = scan->scan_time;
	scan_ori.time_increment = scan->time_increment;
	scan_ori.range_min = scan->range_min;
	scan_ori.range_max = scan->range_max;
	scan_ori.intensities = scan->intensities;
	scan_ori.ranges = scan->ranges;
}

void measureCallback(const std_msgs::Int32MultiArray::ConstPtr& msg)
{
	motor_rpm_0 = msg->data[0];
	motor_rpm_1 = msg->data[1];
	motor_rpm_2 = msg->data[2];
	motor_rpm_3 = msg->data[3];
}


tf::Transform getTransformForMotion(
	double linear_vel_x,
	double linear_vel_y,
	double angular_vel_z,
	double timeSeconds
)
{

	tf::Transform tmp;
	tmp.setIdentity();

	if (std::abs(angular_vel_z) < 0.0001){
		tmp.setOrigin(
			tf::Vector3(
				static_cast<double>(linear_vel_x*timeSeconds),
				static_cast<double>(linear_vel_y*timeSeconds),
				0.0
			)
		);
	}else{
		double distChange_x = linear_vel_x * timeSeconds;
		double distChange_y = linear_vel_y * timeSeconds;
		double angleChange = angular_vel_z * timeSeconds;

		double arcRadius_x = distChange_x / angleChange;
		double arcRadius_y = distChange_y / angleChange;

		tmp.setOrigin(
			tf::Vector3(
				std::sin(angleChange) * arcRadius_x + std::cos(angleChange) * arcRadius_y -arcRadius_y,
				std::sin(angleChange) * arcRadius_y + std::cos(angleChange) * arcRadius_x -arcRadius_x,
				0.0
			)
		);
		tmp.setRotation(tf::createQuaternionFromYaw(angleChange));
	}
	return tmp;

}

int main(int argc, char **argv)
{

	ros::init(argc, argv, "bring_publisher");
	ros::NodeHandle nh;
	

	boost::shared_ptr<ros::NodeHandle> rosnode;
	rosnode.reset(new ros::NodeHandle());
	
	boost::shared_ptr<tf::TransformBroadcaster> transform_broadcaster;
	transform_broadcaster.reset(new tf::TransformBroadcaster());

	//Odom
	ros::Subscriber measure_sub = nh.subscribe("/measure", 100, measureCallback);
	ros::Publisher odom_pub = nh.advertise<nav_msgs::Odometry>("/odom", 100);
	//LiDAR
	ros::Subscriber scan_sub = nh.subscribe("/scan_ori", 100, scanCallback);
	ros::Publisher scan_pub = nh.advertise<sensor_msgs::LaserScan>("/scan", 100);
	//Time
	ros::Publisher time_pub = nh.advertise<std_msgs::Time>("/rostime", 1000);

	
	tf::Transform odom_transform;
	odom_transform.setIdentity();

	ros::Rate loop_rate(100);
	
	ros::Time last_odom_publish_time = ros::Time::now();

	//Gear ratio
	int gear_ratio = 76;
	//radps_to_rpm : rad/sec --> rpm
	double radps_to_rpm = 60.0/2.0/PI;
	//rpm_to_radps : rpm --> rad/sec
	double rpm_to_radps = 2.0*PI/60;

	//Wheel specification in meter
	double wheel_diameter = 0.152;
	double wheel_radius = wheel_diameter/2.0;
	double wheel_separation_a = 0.2355;
	double wheel_separation_b = 0.281;
	double l = wheel_separation_a+wheel_separation_b;

	//Motor speed in rad/sec - initialization
	double wheel_speed_lf = 0;
	double wheel_speed_rf = 0;
	double wheel_speed_lb = 0;
	double wheel_speed_rb = 0;

	double linear_vel_x = 0;
	double linear_vel_y = 0;
	double angular_vel_z = 0;


	while(ros::ok())
	{
		ros::Time currentTime = ros::Time::now();

		nav_msgs::Odometry odom;
		sensor_msgs::LaserScan scan;
		std_msgs::Time rostime;
		tf::TransformBroadcaster broadcaster;

		wheel_speed_lf = (double) motor_rpm_0 * rpm_to_radps / gear_ratio;
		wheel_speed_rf = (double) motor_rpm_1 * rpm_to_radps / gear_ratio;
		wheel_speed_lb = (double) motor_rpm_2 * rpm_to_radps / gear_ratio;
		wheel_speed_rb = (double) motor_rpm_3 * rpm_to_radps / gear_ratio;

		linear_vel_x =
			wheel_radius/4.0*(wheel_speed_lf+wheel_speed_rf+wheel_speed_lb+wheel_speed_rb);

		linear_vel_y =
			wheel_radius/4.0*(-wheel_speed_lf+wheel_speed_rf+wheel_speed_lb-wheel_speed_rb);

		angular_vel_z =
			wheel_radius/(4.0*l)*(-wheel_speed_lf+wheel_speed_rf-wheel_speed_lb+wheel_speed_rb);

		double step_time = 0;
		step_time = currentTime.toSec() - last_odom_publish_time.toSec();
		last_odom_publish_time = currentTime;
		
		odom_transform =
			odom_transform*getTransformForMotion(
				linear_vel_x, linear_vel_y, angular_vel_z, step_time);

		tf::poseTFToMsg(odom_transform, odom.pose.pose);

		scan.header.stamp = currentTime;
		scan.header.frame_id = scan_ori.header.frame_id;
		scan.angle_min = scan_ori.angle_min;
		scan.angle_max = scan_ori.angle_max;
		scan.angle_increment = scan_ori.angle_increment;
		scan.scan_time = scan_ori.scan_time;
		scan.time_increment = scan_ori.time_increment;
		scan.range_min = scan_ori.range_min;
		scan.range_max = scan_ori.range_max;
		scan.intensities = scan_ori.intensities;
		scan.ranges = scan_ori.ranges;

		rostime.data.sec = currentTime.sec;
		rostime.data.nsec = currentTime.nsec;

		odom.twist.twist.angular.z = angular_vel_z;
		odom.twist.twist.linear.x  = linear_vel_x;
		odom.twist.twist.linear.y  = linear_vel_y;

		odom.header.stamp = currentTime;
		odom.header.frame_id = "odom";
		odom.child_frame_id = "base_footprint";

		if (transform_broadcaster.get()){
			transform_broadcaster->sendTransform(
				tf::StampedTransform(
					odom_transform,
					currentTime,
					"odom",
					"base_footprint"
				)
			);
		}
		
		odom.pose.covariance[0] = 0.001;
		odom.pose.covariance[7] = 0.001;
		odom.pose.covariance[14] = 1000000000000.0;
		odom.pose.covariance[21] = 1000000000000.0;
		odom.pose.covariance[28] = 1000000000000.0;
		
		if (std::abs(angular_vel_z) < 0.0001) {
			odom.pose.covariance[35] = 0.01;
		}else{
			odom.pose.covariance[35] = 100.0;
		}

		odom.twist.covariance[0] = 0.001;
		odom.twist.covariance[7] = 0.001;
		odom.twist.covariance[14] = 0.001;
		odom.twist.covariance[21] = 1000000000000.0;
		odom.twist.covariance[28] = 1000000000000.0;
		
		if (std::abs(angular_vel_z) < 0.0001) {
			odom.twist.covariance[35] = 0.01;
		}else{
			odom.twist.covariance[35] = 100.0;
		}

		scan_pub.publish(scan);
		odom_pub.publish(odom);
		time_pub.publish(rostime);
		
		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(0.0, 0.0, 0.0)),
			currentTime,"base_footprint", "base_link"));

		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(-0.281, -0.215, -0.020)),
			currentTime,"base_link", "br_wheel_link"));

		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(-0.281, 0.215, -0.020)),
			currentTime,"base_link", "bl_wheel_link"));

		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(0.281, -0.215, -0.020)),
			currentTime,"base_link", "fr_wheel_link"));

		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(0.281, 0.215, -0.020)),
			currentTime,"base_link", "fl_wheel_link"));

		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(0.0, 0.0, 0.0)),
			currentTime,"base_footprint", "imu_link"));

		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(0.0, 0.0, 0.57)),
			currentTime,"base_link", "base_scan"));
			
		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(tf::createQuaternionFromRPY(0.0, 0.0, 0.0)), tf::Vector3(0.42, 0.0, 0.09)),
			currentTime,"base_link", "base_sonar_front"));
		
		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(tf::createQuaternionFromRPY(0.0, 0.0, PI)), tf::Vector3(-0.41, 0.0, 0.09)),
			currentTime,"base_link", "base_sonar_rear"));

		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(tf::createQuaternionFromRPY(0.0, 0.0, PI/2)), tf::Vector3(0.0, 0.3, 0.09)),
			currentTime,"base_link", "base_sonar_left"));

		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(tf::createQuaternionFromRPY(0.0, 0.0, -PI/2)), tf::Vector3(0.0, -0.3, 0.09)),
			currentTime,"base_link", "base_sonar_right"));

		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(0.41, 0.0, 0.465)),
			currentTime,"base_link", "base_kinect"));

		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(0.0, 0.018, 0.0)),
			currentTime,"base_link", "kinect_depth_frame"));
	
		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(tf::createQuaternionFromRPY(-1.57079632679, 0.0, -1.57079632679)), tf::Vector3(0.0, 0.0, 0.0)),
			currentTime,"base_link", "kinect_depth_optical_frame"));
		
		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(0, 0, 0, 1), tf::Vector3(0.0, -0.005, 0.0)),
			currentTime,"base_link", "kinect_rgb_frame"));
		
		broadcaster.sendTransform(
		tf::StampedTransform(
			tf::Transform(tf::Quaternion(tf::createQuaternionFromRPY(-1.57079632679, 0.0, -1.57079632679)), tf::Vector3(0.0, 0.0, 0.0)),
			currentTime,"base_link", "kinect_rgb_optical_frame"));
		ros::spinOnce();
		loop_rate.sleep();
		
	}

	return 0;

}