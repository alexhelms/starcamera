#include <iostream>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <vector>
#include <sys/time.h>
#include <sys/mman.h>
#include <sched.h>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "tclap/CmdLine.h"
#include "starcamera.h"
#include "starid.h"
#include "getTime.h"

using namespace std;

StarCamera starCam; /*!< Global StarCamera object performs the image aquisition (from file or from camera) and
                    extract bright spots as possible stars for identification */

StarIdentifier starId; /*!< Global StarIdentifier which performs the identification process of the previously
                            extracted star spots (from StarCamera) */

bool printStats; /*!< TODO */

// Parse the command line arguments
// Define possible arguments
TCLAP::CmdLine cmd("Program for attitude estimation from star images",' ', "0.1");

TCLAP::ValueArg<float> epsilon("e", "epsilon", "The allowed tolerance for the feature (in degrees)", false, 0.1, "float");
TCLAP::ValueArg<string> test("", "test", "Run test specified test (all other input will be ignored):\n -camera: Grab a frame from camera and display it on screen", false, string(), "string");
TCLAP::ValueArg<unsigned> area("a", "area", "The minimum area (in pixel) for a spot to be considered for identification", false, 16, "unsigned int");
TCLAP::ValueArg<unsigned> threshold("t", "threshold", "Threshold under which pixels are set to 0", false, 64, "unsigned int");
TCLAP::ValueArg<string> calibrationFile("", "calibration", "Set the calibration file for the camera manually", false, "/home/jan/workspace/usu/starcamera/bin/aptina_12_5mm-calib.txt", "filename");
TCLAP::ValueArg<string> initFile("", "init", "Set the file for initialization of the Aptina camera", false, string(), "filename");
TCLAP::ValueArg<string> kVectorFile("", "kvector", "Set the for loading kVector information", false, "/home/jan/workspace/usu/starcamera/bin/kVectorInput.txt", "filename");

TCLAP::SwitchArg stats("s", "stats", "Print statistics (number of spots, number of identified spots, ratio");
TCLAP::SwitchArg useCamera("c", "camera", "Use the connected Aptina camera as input (input files will be ignored)");
TCLAP::UnlabeledMultiArg<string> files("fileNames", "List of filenames of the raw-image files", false, "file1");


/*!
 \brief Printing function for vector<int>

 \param os
 \param vector
 \return std::ostream &operator
*/
std::ostream & operator << (std::ostream & os, const std::vector<int>& vector)
{
    for(unsigned int i = 0; i<vector.size(); ++i)
    {
        cout << i << '\t' << vector[i] << endl;
    }
    return os;
}

/*!
 \brief Printing function for Spot structure

 \param os
 \param spot
 \return std::ostream &operator
*/
std::ostream & operator << (std::ostream & os, const Spot& spot)
{
    os << spot.center.x << "\t" << spot.center.y << "\t" << spot.area;
    return os;
}

/*!
 \brief Printing function for information of the star-id process

 Prints:
    Coordinates of the center of the extracted star Spot
    The area of the extracted star Spot
    The hip-ID determined for the star Spot (-1 if no identification possible)

 \param os
 \param starID
 \param spots
*/
void outputStats(std::ostream & os, const std::vector<int>& starID, const std::vector<Spot>& spots)
{
    if (starID.size() != spots.size())
        throw std::runtime_error("List of identified spot must have same size as list of extracted spots");
    for(unsigned int i = 0; i<spots.size(); ++i)
    {
        os << spots[i] << "\t" << starID[i] << endl;
    }
}


/*!
 \brief Generates data for the different centroiding functions for each file in files



*/
void centroidingComparison()
{
    vector<string> fileNames = files.getValue();
    for (vector<string>::const_iterator file = fileNames.begin(); file!=fileNames.end(); ++file)
    {
        // for each file extract spots with all methods and print data to files, together with runtime
        starCam.getImageFromFile(file);
        double endTime, startTime;
        vector<double> runtimes;
        vector<vector<Spot> > spotLists;

        // call extract spots with every extraction method and
        // copy results to spotLists and save measured
        // runtime in runtimes
        startTime = getRealTime();
        starCam.extractSpots(StarCamera::ContoursGeometric);
        endTime = getRealTime();
        runtimes.push_back(endTime - startTime);
        spotLists.push_back(starCam.getSpots());

        startTime = getRealTime();
        starCam.extractSpots(StarCamera::ContoursWeighted);
        endTime = getRealTime();
        runtimes.push_back(endTime - startTime);
        spotLists.push_back(starCam.getSpots());

        startTime = getRealTime();
        starCam.extractSpots(StarCamera::ContoursWeightedBoundingBox);
        endTime = getRealTime();
        runtimes.push_back(endTime - startTime);
        spotLists.push_back(starCam.getSpots());

        startTime = getRealTime();
        starCam.extractSpots(StarCamera::ConnectedComponentsGeometric);
        endTime = getRealTime();
        runtimes.push_back(endTime - startTime);
        spotLists.push_back(starCam.getSpots());

        startTime = getRealTime();
        starCam.extractSpots(StarCamera::ConnectedComponentsWeighted);
        endTime = getRealTime();
        runtimes.push_back(endTime - startTime);
        spotLists.push_back(starCam.getSpots());

        /* print the data in the following form (for m>n):
         * File: <filename>
         * <list of runtimes of all methods>
         * <spot1 of method1><spot1 of method2><...>
         * [...]
         * <spot n of method1>spot n of method2><...>
         * <-1><spot n+1 of method2><...>
         * [...]
         * <-1><spot m of method2><...>
         */
        cout << "File: " << file << endl;

        cout << runtimes[0] << "\t" << runtimes[1] << "\t"
             << runtimes[2] << "\t" << runtimes[3] << "\t"
             << runtimes[4] << endl;

        vector<vector<Spot>::const_iterator > it;
        it.push_back(spotLists[0].begin() );
        it.push_back(spotLists[1].begin() );
        it.push_back(spotLists[2].begin() );
        it.push_back(spotLists[3].begin() );
        it.push_back(spotLists[4].begin() );

        while( (it[0]!=spotLists[0].end()) || (it[1]!=spotLists[1].end()) ||
               (it[2]!=spotLists[2].end()) || (it[3]!=spotLists[3].end()) ||
               (it[4]!=spotLists[4].end()))
        {

            for(unsigned i=0; i<5; ++i)
            {
                if (it[i] != spotLists[i].end() )
                {
                    cout << *(it[i]) << "\t";
                    it[i]++;
                }
                else
                {
                    cout << -1 << "\t" << -1 << "\t" << -1 << "\t";
                }
            }
            cout << endl;
        }
    }

}


/*!
 \brief Quick and dirty function atm

 \param eps
*/
void identifyStars(float eps)
{
    starCam.extractSpots(StarCamera::ConnectedComponentsWeighted);
    starCam.calculateSpotVectors();

    //    starId.setFeatureListDB("/home/jan/workspace/usu/starcamera/bin/featureList2.db");
    //    starId.openDb();

    starId.loadFeatureListKVector(kVectorFile.getValue().c_str());

    //    starId.identifyPyramidMethod(starCam.getSpotVectors(), eps);

    std::vector<int> idStars = starId.identifyStars(starCam.getSpotVectors(),eps);

    if(printStats)
        outputStats(cout, idStars, starCam.getSpots());
    else
        cout << idStars;

    cout << endl;
}

/*!
 \brief Function to run a live identification using a picture from the Aptina

 \param eps
*/
void liveIdentification(float eps)
{
    static unsigned counter = 0;

    cout << "File: " << counter << endl;
    starCam.getImage();
    identifyStars(eps);

    counter++;
}

/*!
 \brief Main function

 Handles the commandline arguments, sets the options accordingly and
 runs the program.

 \param argc
 \param argv
 \return int
*/
int main(int argc, char **argv)
{
    /* Avoids memory swapping for this program */
    mlockall(MCL_CURRENT|MCL_FUTURE);

    try
    {
        // Register arguments to parser
        cmd.add(epsilon);
        cmd.add(test);
        cmd.add(area);
        cmd.add(threshold);
        cmd.add(calibrationFile);
        cmd.add(initFile);
        cmd.add(kVectorFile);
        cmd.add(stats);
        cmd.add(useCamera);
        cmd.add(files);

        cmd.parse(argc, argv);

        // check if in test mode
        string testRoutine = test.getValue();
        if(!testRoutine.empty())
        {
            if (testRoutine == "camera")
            {
                starCam.initializeCamera(initFile.getValue());

                starCam.cameraTest();
            }
            if (testRouting == "centroiding")
            {
                centroidingComparison();
            }
            return 0;
        }

        // Get parsed arguments
        float eps = epsilon.getValue();
        starCam.setMinArea(area.getValue() );
        starCam.setThreshold(threshold.getValue());
        starCam.loadCalibration(calibrationFile.getValue().c_str());
        printStats = stats.getValue();

        // check if camera is to be used
        if(useCamera.getValue() )
        {
            if (initFile.getValue().empty())
                starCam.initializeCamera(NULL);
            else
                starCam.initializeCamera(initFile.getValue().c_str());
            liveIdentification(eps); // add options for multiple pictures and delay?
        }
        else // use saved raw images to identifiy stars
        {
            // get the filenames
            std::vector<string> fileNames = files.getValue();

            // for every filename identify the stars and print the results
            for(std::vector<string>::const_iterator file = fileNames.begin(); file != fileNames.end(); ++file)
            {
                starCam.getImageFromFile(file->c_str());

                // print a file identifier
                unsigned pos = file->find_last_of("/\\");
                cout << "File: " << file->substr(pos+1, file->size()-5-pos) << endl;

                identifyStars(eps);
            }

        }
    } catch (TCLAP::ArgException &e)  // catch any exceptions
    {
        std::cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
        return 1;
    }

    return 0;
}

