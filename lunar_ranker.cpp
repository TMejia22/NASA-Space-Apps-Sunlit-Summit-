#include <iostream>
#include <string>
#include <vector>
using namespace std;

int main() {
    cout << "This program determines the landing viability of a particular\ncoordinate on the moon.\n";
    vector<string> say = {"Terrain Safety", "Level of Sunlight", "Communication Quality", "Regolith Quality"};
    vector<float> nums = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    float y, x;
    cout << "\n" << "Degrees Latitude (-90.0 to 90.0): ";
    cin >> y;
    cout << "\n" << "Degrees Longitude (-180.0 to 180.0): ";
    cin >> x;
    float score = 0;
    for (int i = 0; i < 4; i++) {
        cout << "\n" << say[i] << " Percent (0.0 to 100.0): ";
        cin >> nums[i * 2];
        cout << "\n" << "Percent Weight (0.0 to 100.0): ";
        cin >> nums[i * 2 + 1];
        score += (nums[i * 2] * nums[i * 2 + 1] * 0.01);
    }
    score /= 4;
    cout << "\n\n\n" << "The position (" << y << ", " << x << ") has ";
    cout << score << "%" << " landing viability";
    return 0;
}
