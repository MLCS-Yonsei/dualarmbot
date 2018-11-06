#include "ros/ros.h"
#include <std_msgs/Time.h>

int main(int argc, char **argv)
{

	ros::init(argc, argv, "time_publisher");
	ros::NodeHandle nh;

	ros::Publisher time_pub = nh.advertise<std_msgs::Time>("/rostime", 1000);

	// ros::Rate loop_rate(100);

	while(ros::ok())
	{
		std_msgs::Time msg;

		ros::Time current_time = ros::Time::now();

		msg.data.sec = current_time.sec;
		msg.data.nsec = current_time.nsec;

		time_pub.publish(msg);

		// ros::spin();
		// loop_rate.sleep();
	}

	return 0;

}
