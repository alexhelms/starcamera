#include<map>
#include<stdexcept>
#include<fstream>
#include<iostream>
#include<utility>
#include<algorithm>
using std::cout;
using std::endl;

#include "starid.h"


StarIdentifier::StarIdentifier()
    :mDb(NULL), mOpenDb(false)
{
}

StarIdentifier::~StarIdentifier()
{
    // close database (if open) before destroying the object
    sqlite3_close(mDb);
    mOpenDb = false;
}

void StarIdentifier::setFeatureListDB(const std::string filename)
{
    mDbFile = filename;
}

void StarIdentifier::openDb()
{
    if (mDbFile.empty())
    {
        throw std::invalid_argument("No db-file speicfied");
    }
    // close old database (if any was open)
    sqlite3_close(mDb);

    sqlite3 * tempDb;
    // open db-file
    if (sqlite3_open(mDbFile.c_str(), &tempDb) != SQLITE_OK)
    {
        mOpenDb = false;
        throw std::runtime_error("Opening database failed");
    }

    // open new in memory database
    if (sqlite3_open(":memory:", &mDb) != SQLITE_OK)
    {
        mOpenDb = false;
        throw std::runtime_error("Opening database failed");
    }

    // copy database from file to memory for faster access
    sqlite3_backup *backup;
    backup = sqlite3_backup_init(mDb, "main", tempDb, "main");

    if( backup )
    {
        sqlite3_backup_step(backup, -1);
        sqlite3_backup_finish(backup);
    }

    // close database file
    sqlite3_close(tempDb);

    mOpenDb = true;
}

void StarIdentifier::loadFeatureListKVector(const std::string filename)
{
    mFeatureList.clear();
    mKVector.clear();

    std::ifstream ifile;
    ifile.open(filename);

    if(!ifile.is_open())
        throw std::runtime_error("Failed to open k-Vector file");

    ifile >> mQ;
    ifile >> mM;

    do
    {
        int k, hip1, hip2;
        float theta;
        ifile >> k;
        ifile >> hip1;
        ifile >> hip2;
        ifile >> theta;

        mKVector.push_back(k);
        mFeatureList.push_back(Feature2(hip1, hip2, theta));
    } while(!ifile.eof());
}

std::vector<int>  StarIdentifier::identifyStars(const vectorList_t &starVectors, const float eps, StarIdentifier::IdentificationMethod method) const
{

    switch (method)
    {
    case TwoStar:
        return identify2StarMethod(starVectors, eps);
        break;
    case PyramidSQL:
        return identifyPyramidMethod(starVectors, eps);
        break;
    case PyramidKVector:
        return identifyPyramidMethodKVector(starVectors, eps);
        break;
    }

    throw std::invalid_argument("Identification method now present");
}

// define comparison function for finding smallest value in map:
bool pred(const std::pair<int, int>& lhs, const std::pair<int, int>& rhs)
{
 return lhs.second < rhs.second;
}

struct FeatureComparer
{
  FeatureComparer(const int& id1_, const int& id2_) : id1(id1_), id2(id2_) { }
  const int id1, id2;
  bool operator()(const Feature2& lhs) const { return (lhs.id1 == id1 && lhs.id2 == id2);}
};


std::vector<int> StarIdentifier::identify2StarMethod(const vectorList_t &starVectors, const float eps) const
{
    if(!mOpenDb)
        throw std::runtime_error("No Database opened");

    /****************** Voting phase ********************************/

    // create a feature list from the star vectors
    typedef std::vector<Feature2> featureList_t;
    featureList_t featureList;
    createFeatureList2(starVectors, featureList);

    // Vector which holds a map with possible hip for each spot
    typedef std::vector<std::map<int, int> > idTable_t ;
    idTable_t idTable(starVectors.size(), std::map<int,int>() );

    // prepare a statement for the database which searches for the feature/angle within an interval
    std::string sql("SELECT * FROM featureList WHERE theta >? AND theta < ?");
    sqlite3_stmt * sqlStmt;
    if (sqlite3_prepare_v2(mDb, sql.c_str(), sql.size()+1, &sqlStmt, 0) != SQLITE_OK)
        throw std::runtime_error("Prparing SQL search query failed");

    /* Algorithm:
     *  1. Take feature from featureList (done using iterator)
     *  2. search in db for the feature within an interval of 2 epsilon (+-)
     *  3. add all possible hip to the star spot(s) corresponding to the feature (or increase the counter if already existant) using the map
     *  4. Goto 1.
     *  5. For each spots take the hip with the highest counter
     */
    for(featureList_t::const_iterator it = featureList.begin(); it != featureList.end(); ++it)
    {
       float low  = it->theta - eps; // lower bound of interval
       float high = it->theta + eps; // upper bound of interval

       // bind upper and lower bound to the sql statement
       if(sqlite3_reset(sqlStmt) != SQLITE_OK)
           throw std::runtime_error("Resetting SQL query failed");

       if(sqlite3_bind_double(sqlStmt, 1, low) != SQLITE_OK)
           throw std::runtime_error("Binding new value1 to query failed");

       if(sqlite3_bind_double(sqlStmt, 2, high) != SQLITE_OK)
               throw std::runtime_error("Binding new value2 to query failed");

       // execute the query and iterate through the result
       int result;
       while( (result = sqlite3_step(sqlStmt)) == SQLITE_ROW)
       {
           // get the first hip
           int index = sqlite3_column_int(sqlStmt, 0);

           // increase the counter for the hip on both star spots as the problem is symmetric
           // NOTE: an explicit insert for a new hip id is not necessary
           // call is idTable[index of the spot][key for hip]
           idTable[it->id1][index]++;
           idTable[it->id2][index]++;

           // get second hip and increase counter
           index = sqlite3_column_int(sqlStmt, 1);
           idTable[it->id1][index]++;
           idTable[it->id2][index]++;
       }

       if (result != SQLITE_DONE)
           throw std::runtime_error("SQL search returned with unexpected result");
    }

    sqlite3_finalize(sqlStmt);

    /********************************* Validation Phase ***************************************/

    // 5. determine the hip for each star spot
    std::vector<int> idList;  // List with the final identification information for each star
    unsigned nSpots = idTable.size();

    // Initialize idList (-1 for stars with no match or id of the star with most votes)
    int falseStars = 0;
    for (unsigned i=0; i<nSpots; ++i)
    {
        if(idTable[i].empty())
        {
            idList.push_back(-1);
            falseStars++;
        }
        else
        {
            idList.push_back(std::max_element(idTable[i].begin(), idTable[i].end(), pred)->first);
        }
    }

    sql = "SELECT * FROM featureList WHERE hip1 == ? AND hip2 == ?";
    if (sqlite3_prepare_v2(mDb, sql.c_str(), sql.size()+1, &sqlStmt, 0) != SQLITE_OK)
        throw std::runtime_error("Prparing SQL search query failed");

    unsigned unidentifiedStars = 0;
    std::vector<int> votes;
    while(unidentifiedStars < (nSpots-falseStars-1) )
    {
        votes.clear();
        votes.resize(nSpots,0);


        for(unsigned i=0; i<nSpots-1; ++i)
        {
            if(idList[i] < 0)
            {
                votes[i] = nSpots;
                continue;
            }

            for(unsigned j=i+1; j<nSpots; ++j)
            {
                if(idList[j] < 0)
                {
                    votes[j] = nSpots;
                    continue;
                }

                int star1 = idList[i];
                int star2 = idList[j];

                // Get the angle between star1 and star2 in image
                featureList_t::const_iterator angle = std::find_if(featureList.begin(), featureList.end(),
                                                                   FeatureComparer(star1, star2));


                // Look if stars have the same angle as in database
                if(sqlite3_reset(sqlStmt) != SQLITE_OK)
                    throw std::runtime_error("Resetting SQL query failed");

                if(sqlite3_bind_double(sqlStmt, 1, star1) != SQLITE_OK)
                    throw std::runtime_error("Binding new value1 to query failed");

                if(sqlite3_bind_double(sqlStmt, 2, star2) != SQLITE_OK)
                    throw std::runtime_error("Binding new value2 to query failed");

                float theta;
                int result = sqlite3_step(sqlStmt) ;
                if (result == SQLITE_DONE || result == SQLITE_ROW)
                {
                    // get the angle
                    theta = sqlite3_column_int(sqlStmt, 2);
                }
                else
                    throw std::runtime_error("Feature not unique");

                if (fabs(theta-angle->theta) <= eps)
                {
                    votes[i]++;
                    votes[j]++;
                }
            }


        }

        std::vector<int>::iterator minVotes = std::min(votes.begin(), votes.end());
        int minIndex = minVotes - votes.begin();
        unidentifiedStars = *minVotes;

        // Star has unsufficient numbers of votes
         if (unidentifiedStars < (nSpots-falseStars-1) )
         {
             // try a different hip-id for this star spot
             idTable[minIndex][idList[minIndex]] = 0;

              std::map<int,int>::iterator maxElem = std::max_element(idTable[minIndex].begin(), idTable[minIndex].end(), pred);

              if(maxElem->second < 1)
              {
                  idList[minIndex] = -1;
                  falseStars++;
              }
              else
              {
                  idList[minIndex] = maxElem->first;
              }
         }

    }

    sqlite3_finalize(sqlStmt);

    // Remove the hip for stars where the votes are not sufficient enough
    for(unsigned i=0; i<nSpots; ++i)
    {
        if(votes[i] < (nSpots-falseStars-1))
            idList[i] = -1;
    }

    return idList;

}

std::vector<int> StarIdentifier::identifyPyramidMethod(const StarIdentifier::vectorList_t &starVectors, const float eps) const
{
    if(!mOpenDb)
        throw std::runtime_error("No Database opened");


    // prepare a statement for the database which searches for the feature/angle within an interval
    const std::string sqlQuery("SELECT * FROM featureList WHERE theta >? AND theta < ?");
    sqlite3_stmt * sqlStmt;
    if (sqlite3_prepare_v2(mDb, sqlQuery.c_str(), sqlQuery.size()+1, &sqlStmt, 0) != SQLITE_OK)
        throw std::runtime_error("Prparing SQL search query failed");

    // prepare a statement for the database which searches for the feature/angle within an interval and for the correct hip
    const std::string sqlQueryFinal("SELECT * FROM featureList WHERE (theta >? AND theta < ?) AND (hip1 = ? OR hip2 = ?)");
    sqlite3_stmt * sqlFinal;
    if (sqlite3_prepare_v2(mDb, sqlQueryFinal.c_str(), sqlQueryFinal.size()+1, &sqlFinal, 0) != SQLITE_OK)
        throw std::runtime_error("Prparing SQL search query failed");

    /* Algorithm:
     *  1. Take 3 stars (take them in variable order)
     *  2. Calculate 3 angles between them
     *  3. Search feature list for angles from 2.
     *  4. Try to find a unique triangle in the results
     *      - On success:
     *          5. Take 4th star
     *          6. Calculate the 3 new angles between 4th star and 3 identified stars
     *          7. Try to find the 4th star in the catalog
     *          - On success:
     *              - Identification successful, do 5 to 7 for all remaining stars
     *                in order to identify all remaining true and false stars
     *              - On failure:
     *              - Goto 5.
     *       - On failure:
     *          - Goto 1
     */
    int nSpots = starVectors.size();
    if( nSpots < 4)
        throw std::range_error("At least 4 star spots necessary");

    std::vector<int> idList;

    // Stop iteration as soon as one unique triad is identified
    bool identificationComplete = false;

    // iteration in the order suggested by Mortari 2004
    for(int dj=1; dj<(nSpots-1) && !identificationComplete; ++dj)
    {
        for(int dk=1; dk<(nSpots-dj) && !identificationComplete; ++dk)
        {
            for(int i=0; i<(nSpots-dj-dk) && !identificationComplete; ++i)
            {
                int j = i + dj;
                int k = j + dk;
                idList.assign(nSpots, -1);

                // calculate the 3 angles (features)
                const float RAD_TO_DEG = 180 / M_PI;
                // first dot product between each 2 vectors
                float thetaIJ = starVectors[i].dot(starVectors[j]) / (starVectors[i].norm() * starVectors[j].norm() );
                float thetaIK = starVectors[i].dot(starVectors[k]) / (starVectors[i].norm() * starVectors[k].norm() );
                float thetaJK = starVectors[j].dot(starVectors[k]) / (starVectors[j].norm() * starVectors[k].norm() );

                // arccos and conversion to deg
                thetaIJ = acos(thetaIJ) * RAD_TO_DEG;
                thetaIK = acos(thetaIK) * RAD_TO_DEG;
                thetaJK = acos(thetaJK) * RAD_TO_DEG;

                // get a list with possible candidates for each theta
                featureList_t listIJ, listIK, listJK;

                // bind upper and lower bound to the sql statement
                if(sqlite3_reset(sqlStmt) != SQLITE_OK) throw std::runtime_error("Resetting SQL query failed");
                if(sqlite3_bind_double(sqlStmt, 1, thetaIJ - eps) != SQLITE_OK) throw std::runtime_error("Binding new value1 to query failed");
                if(sqlite3_bind_double(sqlStmt, 2, thetaIJ + eps) != SQLITE_OK) throw std::runtime_error("Binding new value2 to query failed");
                retrieveFeatureList(sqlStmt, listIJ);
                // if list is empty skip further processing
                if(listIJ.empty() ) continue;

                // bind upper and lower bound to the sql statement
                if(sqlite3_reset(sqlStmt) != SQLITE_OK) throw std::runtime_error("Resetting SQL query failed");
                if(sqlite3_bind_double(sqlStmt, 1, thetaIK - eps) != SQLITE_OK) throw std::runtime_error("Binding new value1 to query failed");
                if(sqlite3_bind_double(sqlStmt, 2, thetaIK + eps) != SQLITE_OK) throw std::runtime_error("Binding new value2 to query failed");
                retrieveFeatureList(sqlStmt, listIK);
                // if list is empty skip further processing
                if(listIK.empty() ) continue;

                // bind upper and lower bound to the sql statement
                if(sqlite3_reset(sqlStmt) != SQLITE_OK) throw std::runtime_error("Resetting SQL query failed");
                if(sqlite3_bind_double(sqlStmt, 1, thetaJK - eps) != SQLITE_OK) throw std::runtime_error("Binding new value1 to query failed");
                if(sqlite3_bind_double(sqlStmt, 2, thetaJK + eps) != SQLITE_OK) throw std::runtime_error("Binding new value2 to query failed");
                retrieveFeatureList(sqlStmt, listJK);

                // if list is empty skip further processing
                if(listJK.empty() ) continue;


                // find possible triads

                int hipI, hipJ, hipK, count = 0;
                for(featureList_t::const_iterator itIJ = listIJ.begin(), endIJ = listIJ.end(); itIJ != endIJ; ++itIJ)
                {
                    int tempI, tempJ, tempK;
                    for(featureList_t::const_iterator itIK = listIK.begin(), endIK = listIK.end(); itIK != endIK; ++itIK)
                    {
                        if(itIJ->id1 == itIK->id1 || itIJ->id2 == itIK->id1)
                        {
                            tempI = itIK->id1;
                            tempJ = (itIJ->id1 == tempI) ? itIJ->id2 : itIJ->id1;
                            tempK = itIK->id2;
                        }
                        else if(itIJ->id1 == itIK->id2 || itIJ->id2 == itIK->id2)
                        {
                            tempI = itIK->id2;
                            tempJ = (itIJ->id1 == tempI) ? itIJ->id2 : itIJ->id1;
                            tempK = itIK->id1;
                        }
                        else
                        {
                            continue;
                        }

                        for(featureList_t::const_iterator itJK = listJK.begin(), endJK = listJK.end(); itJK != endJK; ++itJK)
                        {
                            if(itJK->id1 == tempK || itJK->id2 == tempK)
                            {
                                if(itJK->id1 == tempJ || itJK->id2 == tempJ)
                                {
                                    hipI = tempI;
                                    hipJ = tempJ;
                                    hipK = tempK;
                                    count++;
                                    break;
                                }
                            }

                        }
                    }
                }

                // if no unique triangle was found try next triad
                if(count != 1)
                {
                    continue;
                }

                idList[i] = hipI;
                idList[j] = hipJ;
                idList[k] = hipK;
                // check if a matching 4th star is found and if identify all remaining spots
                for(int r=0; r<nSpots; ++r)
                {
                    // ignore the stars of the triad
                    if((r == i) || (r == j) || (r == k))
                        continue;

                    // calculate the angles between the new 4th star and the stars of the triad
                    // first dot product between each 2 vectors
                    float thetaIR = starVectors[i].dot(starVectors[r]) / (starVectors[i].norm() * starVectors[r].norm() );
                    float thetaJR = starVectors[j].dot(starVectors[r]) / (starVectors[j].norm() * starVectors[r].norm() );
                    float thetaKR = starVectors[k].dot(starVectors[r]) / (starVectors[k].norm() * starVectors[r].norm() );

                    // arccos and conversion to deg
                    thetaIR = acos(thetaIR) * RAD_TO_DEG;
                    thetaJR = acos(thetaJR) * RAD_TO_DEG;
                    thetaKR = acos(thetaKR) * RAD_TO_DEG;

                    // search in the catalog for the 4th star
                    featureList_t listIR, listJR, listKR;

                    if(sqlite3_reset(sqlFinal) != SQLITE_OK) throw std::runtime_error("Resetting SQL query failed");
                    if(sqlite3_bind_double(sqlFinal, 1, thetaIR - eps) != SQLITE_OK) throw std::runtime_error("Binding new value1 to query failed");
                    if(sqlite3_bind_double(sqlFinal, 2, thetaIR + eps) != SQLITE_OK) throw std::runtime_error("Binding new value2 to query failed");
                    if(sqlite3_bind_int   (sqlFinal, 3, hipI)          != SQLITE_OK) throw std::runtime_error("Binding new value3 to query failed");
                    if(sqlite3_bind_int   (sqlFinal, 4, hipI)          != SQLITE_OK) throw std::runtime_error("Binding new value4 to query failed");
                    retrieveFeatureList(sqlFinal, listIR);
                    // if list is empty skip further processing
                    if(listIR.empty() ) continue;

                    if(sqlite3_reset(sqlFinal) != SQLITE_OK) throw std::runtime_error("Resetting SQL query failed");
                    if(sqlite3_bind_double(sqlFinal, 1, thetaJR - eps) != SQLITE_OK) throw std::runtime_error("Binding new value1 to query failed");
                    if(sqlite3_bind_double(sqlFinal, 2, thetaJR + eps) != SQLITE_OK) throw std::runtime_error("Binding new value2 to query failed");
                    if(sqlite3_bind_int   (sqlFinal, 3, hipJ)          != SQLITE_OK) throw std::runtime_error("Binding new value3 to query failed");
                    if(sqlite3_bind_int   (sqlFinal, 4, hipJ)          != SQLITE_OK) throw std::runtime_error("Binding new value4 to query failed");
                    retrieveFeatureList(sqlFinal, listJR);
                    // if list is empty skip further processing
                    if(listJR.empty() ) continue;

                    if(sqlite3_reset(sqlFinal) != SQLITE_OK) throw std::runtime_error("Resetting SQL query failed");
                    if(sqlite3_bind_double(sqlFinal, 1, thetaKR - eps) != SQLITE_OK) throw std::runtime_error("Binding new value1 to query failed");
                    if(sqlite3_bind_double(sqlFinal, 2, thetaKR + eps) != SQLITE_OK) throw std::runtime_error("Binding new value2 to query failed");
                    if(sqlite3_bind_int   (sqlFinal, 3, hipK)          != SQLITE_OK) throw std::runtime_error("Binding new value3 to query failed");
                    if(sqlite3_bind_int   (sqlFinal, 4, hipK)          != SQLITE_OK) throw std::runtime_error("Binding new value4 to query failed");
                    retrieveFeatureList(sqlFinal, listKR);
                    // if list is empty skip further processing
                    if(listKR.empty() ) continue;


                    // check for a unique solution
                    count = 0;
                    int idCheck, tempID;
                    for(featureList_t::const_iterator itIR=listIR.begin(), endIR = listIR.end(); itIR != endIR; ++itIR)
                    {
                        idCheck = (itIR->id1 == hipI) ? itIR->id2 : itIR->id1;

                        for(featureList_t::const_iterator itJR=listJR.begin(), endJR = listJR.end(); itJR != endJR; ++itJR)
                        {
                            if(itJR->id1 == idCheck || itJR->id2 == idCheck)
                            {
                                for(featureList_t::const_iterator itKR=listKR.begin(), endKR = listKR.end(); itKR != endKR; ++itKR)
                                {
                                    if(itKR->id1 == idCheck || itKR->id2 == idCheck)
                                    {
                                        count++;
                                        tempID = idCheck;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    // if count == 1, everything is good
                    if(count == 1)
                    {
                        idList[r] = tempID;

                        // at least one 4th star found therefore the triad is confirmed and
                        // after the current loop (with r) is through the identification is completed
                        identificationComplete = true;
                    }
                }
            }
        }
    }

    sqlite3_finalize(sqlStmt);
    sqlite3_finalize(sqlFinal);

    return idList;
}

std::vector<int> StarIdentifier::identifyPyramidMethodKVector(const StarIdentifier::vectorList_t &starVectors, const float eps) const
{

    if(mKVector.empty() || mFeatureList.empty() )
        throw std::runtime_error("No feature list loaded");

    /* Algorithm:
     *  1. Take 3 stars (take them in variable order)
     *  2. Calculate 3 angles between them
     *  3. Search feature list for angles from 2.
     *  4. Try to find a unique triangle in the results
     *      - On success:
     *          5. Take 4th star
     *          6. Calculate the 3 new angles between 4th star and 3 identified stars
     *          7. Try to find the 4th star in the catalog
     *          - On success:
     *              - Identification successful, do 5 to 7 for all remaining stars
     *                in order to identify all remaining true and false stars
     *              - On failure:
     *              - Goto 5.
     *       - On failure:
     *          - Goto 1
     */
    int nSpots = starVectors.size();
    if( nSpots < 4)
        throw std::range_error("At least 4 star spots necessary");

    std::vector<int> idList;

    // Stop iteration as soon as one unique triad is identified
    bool identificationComplete = false;

    // iteration in the order suggested by Mortari 2004
    for(int dj=1; dj<(nSpots-1) && !identificationComplete; ++dj)
    {
        for(int dk=1; dk<(nSpots-dj) && !identificationComplete; ++dk)
        {
            for(int i=0; i<(nSpots-dj-dk) && !identificationComplete; ++i)
            {
                int j = i + dj;
                int k = j + dk;
                idList.assign(nSpots, -1);

                // calculate the 3 angles (features)
                const float RAD_TO_DEG = 180 / M_PI;
                // first dot product between each 2 vectors
                float thetaIJ = starVectors[i].dot(starVectors[j]) / (starVectors[i].norm() * starVectors[j].norm() );
                float thetaIK = starVectors[i].dot(starVectors[k]) / (starVectors[i].norm() * starVectors[k].norm() );
                float thetaJK = starVectors[j].dot(starVectors[k]) / (starVectors[j].norm() * starVectors[k].norm() );

                // arccos and conversion to deg
                thetaIJ = acos(thetaIJ) * RAD_TO_DEG;
                thetaIK = acos(thetaIK) * RAD_TO_DEG;
                thetaJK = acos(thetaJK) * RAD_TO_DEG;

                // get a list with possible candidates for each theta
                featureList_t listIJ, listIK, listJK;

                retrieveFeatureListKVector(thetaIJ - eps, thetaIJ + eps, listIJ);
                // if list is empty skip further processing
                if(listIJ.empty() ) continue;

                retrieveFeatureListKVector(thetaIK - eps, thetaIK + eps, listIK);
                // if list is empty skip further processing
                if(listIK.empty() ) continue;

                retrieveFeatureListKVector(thetaJK - eps, thetaJK + eps, listJK);
                // if list is empty skip further processing
                if(listJK.empty() ) continue;

                // find possible triads

                int hipI, hipJ, hipK, count = 0;
                for(featureList_t::const_iterator itIJ = listIJ.begin(), endIJ = listIJ.end(); itIJ != endIJ; ++itIJ)
                {
                    int tempI, tempJ, tempK;
                    for(featureList_t::const_iterator itIK = listIK.begin(), endIK = listIK.end(); itIK != endIK; ++itIK)
                    {
                        if(itIJ->id1 == itIK->id1 || itIJ->id2 == itIK->id1)
                        {
                            tempI = itIK->id1;
                            tempJ = (itIJ->id1 == tempI) ? itIJ->id2 : itIJ->id1;
                            tempK = itIK->id2;
                        }
                        else if(itIJ->id1 == itIK->id2 || itIJ->id2 == itIK->id2)
                        {
                            tempI = itIK->id2;
                            tempJ = (itIJ->id1 == tempI) ? itIJ->id2 : itIJ->id1;
                            tempK = itIK->id1;
                        }
                        else
                        {
                            continue;
                        }

                        for(featureList_t::const_iterator itJK = listJK.begin(), endJK = listJK.end(); itJK != endJK; ++itJK)
                        {
                            if(itJK->id1 == tempK || itJK->id2 == tempK)
                            {
                                if(itJK->id1 == tempJ || itJK->id2 == tempJ)
                                {
                                    hipI = tempI;
                                    hipJ = tempJ;
                                    hipK = tempK;
                                    count++;
                                    break;
                                }
                            }

                        }
                    }
                }

                // if no unique triangle was found try next triad
                if(count != 1)
                {
                    continue;
                }

                idList[i] = hipI;
                idList[j] = hipJ;
                idList[k] = hipK;
                // check if a matching 4th star is found and if identify all remaining spots
                for(int r=0; r<nSpots; ++r)
                {
                    // ignore the stars of the triad
                    if((r == i) || (r == j) || (r == k))
                        continue;

                    // calculate the angles between the new 4th star and the stars of the triad
                    // first dot product between each 2 vectors
                    float thetaIR = starVectors[i].dot(starVectors[r]) / (starVectors[i].norm() * starVectors[r].norm() );
                    float thetaJR = starVectors[j].dot(starVectors[r]) / (starVectors[j].norm() * starVectors[r].norm() );
                    float thetaKR = starVectors[k].dot(starVectors[r]) / (starVectors[k].norm() * starVectors[r].norm() );

                    // arccos and conversion to deg
                    thetaIR = acos(thetaIR) * RAD_TO_DEG;
                    thetaJR = acos(thetaJR) * RAD_TO_DEG;
                    thetaKR = acos(thetaKR) * RAD_TO_DEG;

                    // search in the catalog for the 4th star
                    featureList_t listIR, listJR, listKR;

                    retrieveFeatureListKVector(thetaIR - eps, thetaIR + eps, hipI, listIR);
                    // if list is empty skip further processing
                    if(listIR.empty() ) continue;

                    retrieveFeatureListKVector(thetaJR - eps, thetaJR + eps, hipJ, listJR);
                    // if list is empty skip further processing
                    if(listJR.empty() ) continue;

                    retrieveFeatureListKVector(thetaKR - eps, thetaKR + eps, hipK, listKR);
                    // if list is empty skip further processing
                    if(listKR.empty() ) continue;

                    // check for a unique solution
                    count = 0;
                    int idCheck, tempID = 0;
                    for(featureList_t::const_iterator itIR=listIR.begin(), endIR = listIR.end(); itIR != endIR; ++itIR)
                    {
                        idCheck = (itIR->id1 == hipI) ? itIR->id2 : itIR->id1;

                        for(featureList_t::const_iterator itJR=listJR.begin(), endJR = listJR.end(); itJR != endJR; ++itJR)
                        {
                            if(itJR->id1 == idCheck || itJR->id2 == idCheck)
                            {
                                for(featureList_t::const_iterator itKR=listKR.begin(), endKR = listKR.end(); itKR != endKR; ++itKR)
                                {
                                    if(itKR->id1 == idCheck || itKR->id2 == idCheck)
                                    {
                                        count++;
                                        tempID = idCheck;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    // if count == 1, everything is good
                    if(count == 1)
                    {
                        idList[r] = tempID;

                        // at least one 4th star found therefore the triad is confirmed and
                        // after the current loop (with r) is through the identification is completed
                        identificationComplete = true;
                    }
                }
            }
        }
    }
    return idList;
}

void StarIdentifier::createFeatureList2(const vectorList_t &starVectors, featureList_t &output) const
{
    // start from the beginning
    output.clear();

    const float RAD_TO_DEG = 180 / M_PI;

    vectorList_t::const_iterator it1, end1, it2;
    end1 = starVectors.end() -1 ;

    /* NOTE: i and j are only for counting purposes.
     *       they also could be used for direct accessing the vector elements
     *       but iterators clutter the code less  */
    int i = 0;
    for(it1 = starVectors.begin(); it1 != end1; ++it1, ++i)
    {
        int j = i+1;
        for( it2 = it1+1; it2!= starVectors.end(); ++it2, ++j)
        {
            float dot = it1->dot(*it2) / (it1->norm() * it2->norm()) ;
            // compute the angle between the two vectors and add to the feature list
            float theta = acos(dot ) * RAD_TO_DEG;
            output.push_back(Feature2(i, j, theta) ) ;
        }
    }
}

void StarIdentifier::retrieveFeatureList(sqlite3_stmt * sqlStmt, StarIdentifier::featureList_t &output) const
{
    output.clear();

    int result;
    while( (result = sqlite3_step(sqlStmt)) == SQLITE_ROW)
    {
        // get the first hip
        int hip1 = sqlite3_column_int(sqlStmt, 0);
        int hip2 = sqlite3_column_int(sqlStmt, 1);
        float theta = sqlite3_column_double(sqlStmt, 2);

        output.push_back(Feature2(hip1, hip2, theta));
    }

    if (result != SQLITE_DONE)
        throw std::runtime_error("SQL search returned with unexpected result");
}

void StarIdentifier::retrieveFeatureListKVector(float thetaMin, float thetaMax, StarIdentifier::featureList_t &output) const
{

    output.clear();
    // caclulate k-indices (jb and jt in mortari)
    unsigned jb = (unsigned) ((thetaMin - mQ) / mM);
    unsigned jt = (unsigned) ((thetaMax - mQ) / mM) +1; //always round up

    // calculate bottom and top index from the kVector
    unsigned kb = mKVector[jb] + 1;
    unsigned kt = mKVector[jt];

    for(unsigned i=kb; i<=kt; ++i)
    {
        output.push_back(mFeatureList[i]);
    }
}

void StarIdentifier::retrieveFeatureListKVector(float thetaMin, float thetaMax, int hip, StarIdentifier::featureList_t &output) const
{
    output.clear();
    // caclulate k-indices (jb and jt in mortari)
    unsigned jb = (unsigned) ((thetaMin - mQ) / mM);
    unsigned jt = (unsigned) ((thetaMax - mQ) / mM) +1; //always round up

    // calculate get bottom and to index from the kVector
    unsigned kb = mKVector[jb] + 1;
    unsigned kt = mKVector[jt];

    for(unsigned i=kb; i<=kt; ++i)
    {
        Feature2 temp = mFeatureList[i];
        if (temp.id1 == hip || temp.id2 == hip)
        {
            output.push_back(temp);
        }
    }
}




