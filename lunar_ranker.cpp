#include <iostream>
#include <string>
#include <vector>
using namespace std;

float weighted_average(vector<float> values) {
    float score = 0;
    for (int i = 0; i < (values.size() / 2); i++) {
        score += (values[i * 2] * values[i * 2 + 1] * 0.01);
    }
    return score /= (values.size() / 2);
}

int main() {
    cout << "This program determines the landing viability of a particular\ncoordinate on the moon.\n";
    vector<string> say = {"Terrain Safety", "Level of Sunlight", "Communication Quality", "Regolith Quality"};
    vector<float> nums = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    float y, x;
    cout << "\n" << "Degrees Latitude (-90.0 to 90.0): ";
    cin >> y;
    cout << "\n" << "Degrees Longitude (-180.0 to 180.0): ";
    cin >> x;
    for (int i = 0; i < 4; i++) {
        cout << "\n" << say[i] << " Percent (0.0 to 100.0): ";
        cin >> nums[i * 2];
        cout << "\n" << "Percent Weight (0.0 to 100.0): ";
        cin >> nums[i * 2 + 1];
    }
    
    cout << "\n\n" << "The position (" << x << ", " << y << ") has ";
    cout << weighted_average(nums) << "%" << " landing viability";
    return 0;
}
