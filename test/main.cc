#include <iostream>
#include <vector>
#include <unordered_map>
using namespace std;

int process(int num)
{
    int ans = 0;
    while (num != 0)
    {
        ans += num & 1;
        num = num >> 1;
    }
    return ans;
}
int maximumSum(vector<int> &nums)
{
    int ans = -1;
    vector<int> processed_nums;
    for (int i = 0; i < nums.size(); i++)
    {
        processed_nums.push_back(process(nums[i]));
    }
    unordered_map<int, int> cnt;
    for (int i = 0; i < nums.size(); i++)
    {
        if (cnt.find(processed_nums[i]) != cnt.end())
        {
            auto it = cnt.find(processed_nums[i]);
            int sum = nums[i] + nums[it->second];
            ans = max(ans, sum);
            if (nums[i] > nums[it->second])
            {
                cnt[processed_nums[i]] = i;
            }
        }
        else
        {
            cnt[processed_nums[i]] = i;
        }
    }
    return ans;
}
int main()
{
    vector<int> nums = {10, 12, 19, 14};
    int ans = maximumSum(nums);
    cout << ans << endl;
}