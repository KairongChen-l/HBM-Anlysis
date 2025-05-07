#include<iostream>
#include<bits/stdc++.h>
using namespace std;

int subarraysDivByK(vector<int> &nums, int k)
{
    int sum = 0;
    unordered_map<int,int> mp;
    int ans = 0;
    mp[0]++;
    for(auto x:nums){
        sum += x;
        int tmp = k-(sum%k);
        if(mp.find(tmp) != mp.end()){
            ans += mp[tmp];
        }
        mp[(sum%k)]++;
    }
    return ans;
}

int main(){
    vector<int> nums = {4, 5, 0, -2, -3, 1};
    int k = 5;
    int ans = subarraysDivByK(nums, k);
    cout << ans << endl;
}