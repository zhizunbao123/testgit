#include <glog/logging.h>
#include "openposeConnector.hpp"
#include "fastMath.hpp"

template <typename T>
void connectBodyPartsCpu(std::vector< std::vector< cv::Vec<T, 3> > >& poseKeyPoints, const T* const heatMapPtr, const T* const peaksPtr, const property::PoseModel poseModel, const cv::Size& heatMapSize, const T scaleFactor)
{
    const int interMinAboveThreshold=property::POSE_DEFAULT_CONNECT_INTER_MIN_ABOVE_THRESHOLD[(int)poseModel];
    const T interThreshold=property::POSE_DEFAULT_CONNECT_INTER_THRESHOLD[(int)poseModel];
    const int minSubsetCnt=property::POSE_DEFAULT_CONNECT_MIN_SUBSET_CNT[(int)poseModel];
    const T minSubsetScore=property::POSE_DEFAULT_CONNECT_MIN_SUBSET_SCORE[(int)poseModel];
    const int maxPeaks=property::POSE_MAX_PEOPLE;

    // Parts Connection
    const auto& bodyPartPairs = property::POSE_BODY_PART_PAIRS[(int)poseModel];
    const auto& mapIdx = property::POSE_MAP_IDX[(int)poseModel];
    const auto numberBodyParts = property::POSE_NUMBER_BODY_PARTS[(int)poseModel];
    const auto numberBodyPartPairs = bodyPartPairs.size() / 2;

    std::vector<std::pair<std::vector<int>, double>> subset;    // Vector<int> = Each body part + body parts counter; double = subsetScore
    const auto subsetCounterIndex = numberBodyParts+1;
    const auto subsetSize = numberBodyParts+2;

    const auto peaksOffset = 3*(maxPeaks+1);
    const auto heatMapOffset = heatMapSize.area();

    for (size_t pairIndex = 0; pairIndex < numberBodyPartPairs; pairIndex++)
    {
        const auto bodyPartA = bodyPartPairs[2*pairIndex];
        const auto bodyPartB = bodyPartPairs[2*pairIndex+1];
        const auto* candidateA = peaksPtr + bodyPartA*peaksOffset;
        const auto* candidateB = peaksPtr + bodyPartB*peaksOffset;
        const auto nA = intRound(candidateA[0]);
        const auto nB = intRound(candidateB[0]);

        // add parts into the subset in special case
        if (nA == 0 || nB == 0)
        {
            // Change w.r.t. other
            if (nB != 0)
            {
                if (poseModel == property::PoseModel::COCO_18)
                {
                    for (auto i = 1; i <= nB; i++)
                    {
                        bool num = false;
                        const auto indexB = bodyPartB;
                        for (size_t j = 0; j < subset.size(); j++)
                        {
                            const auto off = (int)bodyPartB*peaksOffset + i*3 + 2;
                            if (subset[j].first[indexB] == off)
                            {
                                num = true;
                                break;
                            }
                        }
                        if (!num)
                        {
                            std::vector<int> rowVector(subsetSize, 0);
                            rowVector[ bodyPartB ] = bodyPartB*peaksOffset + i*3 + 2; //store the index
                            rowVector[subsetCounterIndex] = 1; //last number in each row is the parts number of that person
                            const auto subsetScore = candidateB[i*3+2]; //second last number in each row is the total score
                            subset.emplace_back(std::make_pair(rowVector, subsetScore));
                        }
                    }
                }
                else if (poseModel == property::PoseModel::MPI_15 || poseModel == property::PoseModel::MPI_15_4)
                {
                    for (auto i = 1; i <= nB; i++)
                    {
                        std::vector<int> rowVector(subsetSize, 0);
                        rowVector[ bodyPartB ] = bodyPartB*peaksOffset + i*3 + 2; //store the index
                        rowVector[subsetCounterIndex] = 1; //last number in each row is the parts number of that person
                        const auto subsetScore = candidateB[i*3+2]; //second last number in each row is the total score
                        subset.emplace_back(std::make_pair(rowVector, subsetScore));
                    }
                }
                else
                    LOG(FATAL) << "Unknown model, cast to int = " + std::to_string((int)poseModel) << std::endl;
            }
            else if (nA != 0)
            {
                if (poseModel == property::PoseModel::COCO_18)
                {
                    for (auto i = 1; i <= nA; i++)
                    {
                        bool num = false;
                        const auto indexA = bodyPartA;
                        for (size_t j = 0; j < subset.size(); j++)
                        {
                            const auto off = (int)bodyPartA*peaksOffset + i*3 + 2;
                            if (subset[j].first[indexA] == off)
                            {
                                num = true;
                                break;
                            }
                        }
                        if (!num)
                        {
                            std::vector<int> rowVector(subsetSize, 0);
                            rowVector[ bodyPartA ] = bodyPartA*peaksOffset + i*3 + 2; //store the index
                            rowVector[subsetCounterIndex] = 1; //last number in each row is the parts number of that person
                            const auto subsetScore = candidateA[i*3+2]; //second last number in each row is the total score
                            subset.emplace_back(std::make_pair(rowVector, subsetScore));
                        }
                    }
                }
                else if (poseModel == property::PoseModel::MPI_15 || poseModel == property::PoseModel::MPI_15_4)
                {
                    for (auto i = 1; i <= nA; i++)
                    {
                        std::vector<int> rowVector(subsetSize, 0);
                        rowVector[ bodyPartA ] = bodyPartA*peaksOffset + i*3 + 2; //store the index
                        rowVector[subsetCounterIndex] = 1; //last number in each row is the parts number of that person
                        const auto subsetScore = candidateA[i*3+2]; //second last number in each row is the total score
                        subset.emplace_back(std::make_pair(rowVector, subsetScore));
                    }
                }
                else
                    LOG(FATAL) << "Unknown model, cast to int = " + std::to_string((int)poseModel) << std::endl;
            }
        }
        else
        {
            std::vector<std::tuple<double, int, int>> temp;
            const auto numInter = 10;
            const auto* const mapX = heatMapPtr + mapIdx[2*pairIndex] * heatMapOffset;
            const auto* const mapY = heatMapPtr + mapIdx[2*pairIndex+1] * heatMapOffset;
            for (auto i = 1; i <= nA; i++)
            {
                for (auto j = 1; j <= nB; j++)
                {
                    const auto dX = candidateB[j*3] - candidateA[i*3];
                    const auto dY = candidateB[j*3+1] - candidateA[i*3+1];
                    const auto normVec = T(std::sqrt( dX*dX + dY*dY ));
                    // If the peaksPtr are coincident. Don't connect them.
                    if (normVec > 1e-6)
                    {
                        const auto sX = candidateA[i*3];
                        const auto sY = candidateA[i*3+1];
                        const auto vecX = dX/normVec;
                        const auto vecY = dY/normVec;

                        auto sum = 0.;
                        auto count = 0;
                        for (auto lm=0; lm < numInter; lm++)
                        {
                            const auto mX = fastMin(heatMapSize.width-1, intRound(sX + lm*dX/numInter));
                            const auto mY = fastMin(heatMapSize.height-1, intRound(sY + lm*dY/numInter));
                            CHECK_GE(mX, 0);
                            CHECK_GE(mY, 0);
                            const auto idx = mY * heatMapSize.width + mX;
                            const auto score = (vecX*mapX[idx] + vecY*mapY[idx]);
                            if (score > interThreshold)
                            {
                                sum += score;
                                count++;
                            }
                        }

                        // parts score + cpnnection score
                        if (count > interMinAboveThreshold)
                            temp.emplace_back(std::make_tuple(sum/count, i, j));
                    }
                }
            }

            // select the top minAB connection, assuming that each part occur only once
            // sort rows in descending order based on parts + connection score
            if (!temp.empty())
                std::sort(temp.begin(), temp.end(), std::greater<std::tuple<T, int, int>>());

            std::vector<std::tuple<int, int, double>> connectionK;

            const auto minAB = fastMin(nA, nB);
            std::vector<int> occurA(nA, 0);
            std::vector<int> occurB(nB, 0);
            auto counter = 0;
            for (size_t row = 0; row < temp.size(); row++)
            {
                const auto score = std::get<0>(temp[row]);
                const auto x = std::get<1>(temp[row]);
                const auto y = std::get<2>(temp[row]);
                if (!occurA[x-1] && !occurB[y-1])
                {
                    connectionK.emplace_back(std::make_tuple(bodyPartA*peaksOffset + x*3 + 2,
                                bodyPartB*peaksOffset + y*3 + 2,
                                score));
                    counter++;
                    if (counter==minAB)
                        break;
                    occurA[x-1] = 1;
                    occurB[y-1] = 1;
                }
            }

            // Cluster all the body part candidates into subset based on the part connection
            // initialize first body part connection 15&16
            if (pairIndex==0)
            {
                for (const auto connectionKI : connectionK)
                {
                    std::vector<int> rowVector(numberBodyParts+3, 0);
                    const auto indexA = std::get<0>(connectionKI);
                    const auto indexB = std::get<1>(connectionKI);
                    const auto score = std::get<2>(connectionKI);
                    rowVector[bodyPartPairs[0]] = indexA;
                    rowVector[bodyPartPairs[1]] = indexB;
                    rowVector[subsetCounterIndex] = 2;
                    // add the score of parts and the connection
                    const auto subsetScore = peaksPtr[indexA] + peaksPtr[indexB] + score;
                    subset.emplace_back(std::make_pair(rowVector, subsetScore));
                }
            }
            // // Add ears connections (in case person is looking to opposite direction to camera)
            // else if (poseModel == PoseModel::COCO_18 && (pairIndex==16 || pairIndex==17))
            // {
            //     for (const auto& connectionKI : connectionK)
            //     {
            //         const auto indexA = std::get<0>(connectionKI);
            //         const auto indexB = std::get<1>(connectionKI);
            //         for (auto& subsetJ : subset)
            //         {
            //             auto& subsetJFirst = subsetJ.first[bodyPartA]; 
            //             auto& subsetJFirstPlus1 = subsetJFirst[bodyPartB]; 
            //             if (subsetJFirst == indexA && subsetJFirstPlus1 == 0)
            //                 subsetJFirstPlus1 = indexB;
            //             else if (subsetJFirstPlus1 == indexB && subsetJFirst == 0)
            //                 subsetJFirst = indexA;
            //         }
            //     }
            // }
            else
            {
                if (!connectionK.empty())
                {
                    // A is already in the subset, find its connection B
                    for (size_t i = 0; i < connectionK.size(); i++)
                    {
                        const auto indexA = std::get<0>(connectionK[i]);
                        const auto indexB = std::get<1>(connectionK[i]);
                        const auto score = std::get<2>(connectionK[i]);
                        auto num = 0;
                        for (size_t j = 0; j < subset.size(); j++)
                        {
                            if (subset[j].first[bodyPartA] == indexA)
                            {
                                subset[j].first[bodyPartB] = indexB;
                                num++;
                                subset[j].first[subsetCounterIndex] = subset[j].first[subsetCounterIndex] + 1;
                                subset[j].second = subset[j].second + peaksPtr[indexB] + score;
                            }
                        }
                        // if can not find partA in the subset, create a new subset
                        if (num==0)
                        {
                            std::vector<int> rowVector(subsetSize, 0);
                            rowVector[bodyPartA] = indexA;
                            rowVector[bodyPartB] = indexB;
                            rowVector[subsetCounterIndex] = 2;
                            const auto subsetScore = peaksPtr[indexA] + peaksPtr[indexB] + score;
                            subset.emplace_back(std::make_pair(rowVector, subsetScore));
                        }
                    }
                }
            }
        }
    }

    // Delete people below the following thresholds:
    // a) minSubsetCnt: removed if less than minSubsetCnt body parts
    // b) minSubsetScore: removed if global score smaller than this
    // c) POSE_MAX_PEOPLE: keep first POSE_MAX_PEOPLE people above thresholds
    auto numberPeople = 0;
    std::vector<int> validSubsetIndexes;
    validSubsetIndexes.reserve(std::min((size_t)property::POSE_MAX_PEOPLE, subset.size()));
    for (size_t index = 0 ; index < subset.size() ; index++)
    {
        const auto subsetCounter = subset[index].first[subsetCounterIndex];
        const auto subsetScore = subset[index].second;
        if (subsetCounter >= minSubsetCnt && (subsetScore/subsetCounter) > minSubsetScore)
        {
            numberPeople++;
            validSubsetIndexes.emplace_back(index);
            if (numberPeople == (int)property::POSE_MAX_PEOPLE)
                break;
        }
        else if (subsetCounter < 1)
            LOG(FATAL) << "Bad subsetCounter. Bug in in this function if this happens." << std::endl;
    }

    // Fill and return poseKeyPoints
    if (numberPeople > 0)
        poseKeyPoints.resize(numberPeople);

    for (size_t person = 0 ; person < validSubsetIndexes.size() ; person++)
    {
        poseKeyPoints[person].resize(numberBodyParts);
        const auto& subsetI = subset[validSubsetIndexes[person]].first;
        for (auto bodyPart = 0u; bodyPart < numberBodyParts; bodyPart++)
        {
            const auto bodyPartIndex = subsetI[bodyPart];
            if (bodyPartIndex > 0)
            {
                poseKeyPoints[person][bodyPart][0] = peaksPtr[bodyPartIndex-2] * scaleFactor;
                poseKeyPoints[person][bodyPart][1] = peaksPtr[bodyPartIndex-1] * scaleFactor;
                poseKeyPoints[person][bodyPart][2] = peaksPtr[bodyPartIndex];
            }
            else
            {
                poseKeyPoints[person][bodyPart][0] = 0.f;
                poseKeyPoints[person][bodyPart][1] = 0.f;
                poseKeyPoints[person][bodyPart][2] = 0.f;
            }
        }
    }

}


template void connectBodyPartsCpu(std::vector< std::vector< cv::Vec<float, 3> > >& poseKeyPoints, const float* const heatMapPtr, const float* const peaksPtr, const property::PoseModel poseModel, const cv::Size& heatMapSize, const float scaleFactor);

template void connectBodyPartsCpu(std::vector< std::vector< cv::Vec<double, 3> > >& poseKeyPoints, const double* const heatMapPtr, const double* const peaksPtr, const property::PoseModel poseModel, const cv::Size& heatMapSize, const double scaleFactor);
