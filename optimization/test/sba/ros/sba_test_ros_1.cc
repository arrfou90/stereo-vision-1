#include "../../sba/extern/ros_sba/include/sba/sba.h"
#include "../../sba/extern/ros_sba/include/sba/sba_file_io.h"

#include <time.h>

#include <eigen3/Eigen/Core>

using namespace sba;
using namespace std;
using namespace Eigen;


Eigen::Matrix4d getCameraRt(SysSBA& sba, int ci)
{
  Eigen::Matrix4d mat = Eigen::Matrix4d::Identity();
  mat.block<3,3>(0,0) = sba.nodes[ci].qrot.toRotationMatrix();
  mat.col(3) = sba.nodes[ci].trans;
  return mat;
}

void setupSBA(SysSBA &sys)
{
  // Create camera parameters.
  frame_common::CamParams cam_params;
  cam_params.fx = 430; // Focal length in x
  cam_params.fy = 430; // Focal length in y
  cam_params.cx = 320; // X position of principal point
  cam_params.cy = 240; // Y position of principal point
  //cam_params.tx = 0;   // Baseline (no baseline since this is monocular)
  cam_params.tx = 0.5;   // Baseline (no baseline since this is monocular)

  // Define dimensions of the image.
  int maxx = 640;
  int maxy = 480;

  // Create a plane containing a wall of points.
  int npts_x = 10; // Number of points on the plane in x
  int npts_y = 5;  // Number of points on the plane in y

  double plane_width = 5;     // Width of the plane on which points are positioned (x)
  double plane_height = 2.5;    // Height of the plane on which points are positioned (y)
  double plane_distance = 5; // Distance of the plane from the cameras (z)

  // Vector containing the true point positions.
  vector<Point, Eigen::aligned_allocator<Point>> points;

  for (int ix = 0; ix < npts_x ; ix++)
  {
    for (int iy = 0; iy < npts_y ; iy++)
    {
      // Create a point on the plane in a grid.
      points.push_back(Point(plane_width/npts_x*(ix+.5), -plane_height/npts_y*(iy+.5), plane_distance, 1.0));
    }
  }

  // Create nodes and add them to the system.
  unsigned int nnodes = 5; // Number of nodes.
  double path_length = 3; // Length of the path the nodes traverse.

  unsigned int i = 0, j = 0;

  for (i = 0; i < nnodes; i++)
  { 
    // Translate in the x direction over the node path.
    Vector4d trans(i/(nnodes-1.0)*path_length, 0, 0, 1);

    // Don't rotate.
    Quaterniond rot(1, 0, 0, 0);
    rot.normalize();

    // Add a new node to the system.
    sys.addNode(trans, rot, cam_params, false);
  }

  // Set the random seed.
  unsigned short seed = (unsigned short)time(NULL);
  seed48(&seed);

  // Add points into the system, and add noise.
  for (i = 0; i < points.size(); i++)
  {
    // Add up to .5 points of noise.
    Vector4d temppoint = points[i];
    temppoint.x() += drand48() - 0.5;
    temppoint.y() += drand48() - 0.5;
    temppoint.z() += drand48() - 0.5;
    //sys.addPoint(temppoint);
    // no noise
    sys.addPoint(points[i]);
  }

  Vector2d proj_mono;
  Vector3d proj;

  // Project points into nodes.
  for (i = 0; i < points.size(); i++)
  {
    for (j = 0; j < sys.nodes.size(); j++)
    {
      // Project the point into the node's image coordinate system.
      sys.nodes[j].setProjection();
      sys.nodes[j].project2im(proj_mono, points[i]);

      // calc stereo projection in right camera
      sys.nodes[j].setProjection();
      sys.nodes[j].projectStereo(points[i], proj);
      std::cout << proj << "\n\n";

      // If valid (within the range of the image size), add the monocular 
      // projection to SBA.
      if (proj.x() > 0 && proj.x() < maxx && proj.y() > 0 && proj.y() < maxy)
      {
        //sys.addMonoProj(j, i, proj_mono);
        sys.addStereoProj(j, i, proj);
      }
    }
  }

  // Add noise to node position.

  double transscale = 1.0;
  double rotscale = 0.2;

  // Don't actually add noise to the first node, since it's fixed.
  for (i = 1; i < sys.nodes.size(); i++)
  {
    Vector4d temptrans = sys.nodes[i].trans;
    Quaterniond tempqrot = sys.nodes[i].qrot;

    // Add error to both translation and rotation.
    temptrans.x() += transscale*(drand48() - 0.5);
    temptrans.y() += transscale*(drand48() - 0.5);
    temptrans.z() += transscale*(drand48() - 0.5);
    tempqrot.x() += rotscale*(drand48() - 0.5);
    tempqrot.y() += rotscale*(drand48() - 0.5);
    tempqrot.z() += rotscale*(drand48() - 0.5);
    tempqrot.normalize();

    sys.nodes[i].trans = temptrans;
    sys.nodes[i].qrot = tempqrot;

    // These methods should be called to update the node.
    sys.nodes[i].normRot();
    sys.nodes[i].setTransform();
    sys.nodes[i].setProjection();
    sys.nodes[i].setDr(true);
  }

}

void processSBA()
{
  // Create an empty SBA system.
  SysSBA sys;

  setupSBA(sys);

  // Provide some information about the data read in.
  printf("Cameras (nodes): %d, Points: %d\n\n",
      (int)sys.nodes.size(), (int)sys.tracks.size());

  std::cout << "Rt before:\n";
  for(int i = 0; i < sys.nodes.size(); i++) {
    Eigen::Matrix4d Rt = getCameraRt(sys, i);
    std::cout << Rt << std::endl;
  }

  // Perform SBA with 10 iterations, an initial lambda step-size of 1e-3, 
  // and using CSPARSE.
  sys.doSBA(10, 1e-3, SBA_SPARSE_CHOLESKY);

  std::cout << "Rt after:\n";
  for(int i = 0; i < sys.nodes.size(); i++) {
    Eigen::Matrix4d Rt = getCameraRt(sys, i);
    std::cout << Rt << std::endl;
  }

  int npts = sys.tracks.size();

  printf("Bad projs (> 10 pix): %d, Cost without: %f", 
      (int)sys.countBad(10.0), sqrt(sys.calcCost(10.0)/npts));
  printf("Bad projs (> 5 pix): %d, Cost without: %f", 
      (int)sys.countBad(5.0), sqrt(sys.calcCost(5.0)/npts));
  printf("Bad projs (> 2 pix): %d, Cost without: %f", 
      (int)sys.countBad(2.0), sqrt(sys.calcCost(2.0)/npts));

  printf("Cameras (nodes): %d, Points: %d\n\n",
      (int)sys.nodes.size(), (int)sys.tracks.size());
}

int main(int argc, char **argv)
{
  processSBA();

  return 0;
}

