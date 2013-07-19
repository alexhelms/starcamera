#include <iostream>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <vector>
#include <sys/time.h>
#include <sys/mman.h>
#include <sched.h>
#include <string>
#include <fstream>


#include "starcamera.h"
#include "starid.h"

using namespace std;

int timeval_subtract (struct timeval * result, struct timeval * x, struct timeval * y)
{
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_usec < y->tv_usec) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
        y->tv_usec -= 1000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
        int nsec = (x->tv_usec - y->tv_usec) / 1000000;
        y->tv_usec += 1000000 * nsec;
        y->tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
          tv_usec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}

int main(int argc, char **argv)
{
//    struct sched_param param;

//    param.__sched_priority = 50;

//    if( sched_setscheduler( 0, SCHED_FIFO, &param ) == -1 )
//    {
//        perror("sched_setscheduler");
//    }

    if(argc < 2)
    {
        cout << "Error: no filename for raw file" << endl;
        return 1;
    }

    /* Avoids memory swapping for this program */
//    mlockall(MCL_CURRENT|MCL_FUTURE);

    StarCamera starCam;
    starCam.setMinRadius(3.0f);
    starCam.mMinArea = 16;
    starCam.loadCalibration("/home/jan/workspace/usu/starcamera/bin/aptina_12_5mm-calib.txt");

    starCam.getImageFromFile(argv[1]);

    starCam.ConnectedComponentsWeighted();
    starCam.calculateSpotVectors();

    StarIdentifier starId;

    starId.setFeatureListDB("/home/jan/workspace/usu/starcamera/bin/featureList2.db");
    starId.openDb();

    starId.loadFeatureListKVector("/home/jan/workspace/usu/starcamera/bin/kVectorInput.txt");

    starId.identifyPyramidMethod(starCam.getSpotVectors(),0.15);

    starId.identifyPyramidMethodKVector(starCam.getSpotVectors(),0.15);



    cout << "Finished" << endl;

    return 0;
}

//int main(int argc, char **argv)
//{

//    struct sched_param param;

//    param.__sched_priority = 50;

//    if( sched_setscheduler( 0, SCHED_FIFO, &param ) == -1 )
//    {
//        perror("sched_setscheduler");
//    }

//    if(argc < 2)
//    {
//        cout << "Error: no filename for raw file" << endl;
//        return 1;
//    }

//    int time = 0;

//    /* Avoids memory swapping for this program */
//    mlockall(MCL_CURRENT|MCL_FUTURE);

//    StarCamera starCam;
//    starCam.setMinRadius(3.0f);
//    starCam.mMinArea = 16;
//    starCam.loadCalibration("aptina_12_5mm-calib.txt");

//    for(int file=1; file<argc; ++file)
//    {

//        int timeGeometric;
//        int timeWeighted;
//        int timeWeightedBoundingRect;
//        int timeCC;
//        int timeCCWeighted;

//        starCam.getImageFromFile(argv[file]);

//        std::string s1(argv[file]);

//       struct timeval start, now, time2, elapsed;

//       // Measure time for geometric centroiding

//        gettimeofday(&start, NULL);
//        starCam.extractSpots();
//        gettimeofday(&now, NULL);
//        timeval_subtract(&elapsed, &now, &start);

//        timeGeometric = elapsed.tv_sec * 1000000 + elapsed.tv_usec;

//        // Measure time for weighted centroiding

//        gettimeofday(&start, NULL);
//        starCam.WeightedCentroiding();
//        gettimeofday(&now, NULL);
//        timeval_subtract(&elapsed, &now, &start);

//        timeWeighted= elapsed.tv_sec * 1000000 + elapsed.tv_usec;

//        // Measure time for weighted centroiding using the bounding rectangle

//        gettimeofday(&start, NULL);
//        starCam.WeightedCentroidingBoundingRect();
//        gettimeofday(&now, NULL);
//        timeval_subtract(&elapsed, &now, &start);

//        timeWeightedBoundingRect = elapsed.tv_sec * 1000000 + elapsed.tv_usec;

//        // Measure time for connected components with geometric centroiding

//        gettimeofday(&start, NULL);
//        starCam.ConnectedComponents();
//        gettimeofday(&now, NULL);
//        timeval_subtract(&elapsed, &now, &start);

//        timeCC = elapsed.tv_sec * 1000000 + elapsed.tv_usec;

//        // Measure time for connected components with weighted centroiding

//        gettimeofday(&start, NULL);
//        starCam.ConnectedComponentsWeighted();
//        gettimeofday(&now, NULL);
//        timeval_subtract(&elapsed, &now, &start);

//        timeCCWeighted = elapsed.tv_sec * 1000000 + elapsed.tv_usec;


//        // Print all the times

//        cout << s1.substr(s1.find_last_of("\\/")+1) << "\t"
//             << timeGeometric << "\t"
//             << timeWeighted  << "\t"
//             << timeWeightedBoundingRect << "\t"
//             << timeCC << "\t"
//             << timeCCWeighted << endl;

////        vector<StarCamera::Spot>::iterator it;
////        for(it = starCam.mSpots.begin(); it != starCam.mSpots.end(); ++it)
////        {
////            cv::circle(starCam.mFrame, it->center, it->radius, cv::Scalar(255), 2);
////        }

////        starCam.calculateSpotVectors();

////        gettimeofday(&time2, NULL);

////        cv::imwrite("test1.png", starCam.mLabels);

////        cout << endl << "File: " << s1.substr(s1.find_last_of("\\/")+1)  << endl;

////        std::vector<StarCamera::Spot>::const_iterator spot;
////        for (spot = starCam.mSpots.begin(); spot!= starCam.mSpots.end(); ++spot)
////        {
////            cout << spot->center.x << "\t" << 1944- spot->center.y << "\t" "\t";
////            cout << spot->centroid1.x << "\t" << spot->centroid1.y << "\t";
////            cout << spot->centroid2.x << "\t" << spot->centroid2.y << endl;
////        }


////        std::vector<StarCamera::Spot2>::const_iterator spot;
////        for (spot = starCam.mTestSpots.begin(); spot!= starCam.mTestSpots.end(); ++spot)
////        {
////            cout << spot->center.x << "\t" << 1944- spot->center.y << "\t" << spot->area << endl;
////        }

////        timeval_subtract(&elapsed, &now, &start);

////        int us_extract = elapsed.tv_sec * 1000000 + elapsed.tv_usec;

////        timeval_subtract(&elapsed, &time2, &now);

////        int us_vectors = elapsed.tv_sec * 1000000 + elapsed.tv_usec;

////        timeval_subtract(&elapsed, &time2, &start);

////        int us_total = elapsed.tv_sec * 1000000 + elapsed.tv_usec;

////        cout << us_extract << "\t" << us_vectors << "\t" << us_total << endl << endl;

////        time += us_total;


////        timeval_subtract(&elapsed, &time2, &start);
////        cout << "time: " << us_vectors << endl;
////        cout << "labels: " << n << endl;
////        cout << "old-time:" << us_extract << endl;

//    }

////    cout << "average " << 1.0 * time / (argc-1) << endl;

//    return 0;
//}

