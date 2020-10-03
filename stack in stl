#include <iostream>
#include <stack>
#include <string>
#include <cstdlib>
using namespace std;
int main() {
   stack<int> s;
   int c, i;
   while (1) {
      cout<<"1.Size of the Stack"<<endl;
      cout<<"2.Insert Element into the Stack"<<endl;
      cout<<"3.Delete Element from the Stack"<<endl;
      cout<<"4.Top Element of the Stack"<<endl;
      cout<<"5.Exit"<<endl;
      cout<<"Enter your Choice: ";
      cin>>c;
      switch(c) {
         case 1:
            cout<<"Size of the stack: ";
            cout<<s.size()<<endl;
         break;
         case 2:
            cout<<"Enter value to be inserted: ";
            cin>>i;
            s.push(i);
         break;
         case 3:
            i = s.top();
            if (!s.empty()) {
               s.pop();
               cout<<i<<" Deleted"<<endl;
            }else {
               cout<<"Stack is Empty"<<endl;
            }
         break;
         case 4:
            cout<<"Top Element of the Stack: ";
            cout<<s.top()<<endl;
            break;
         case 5:
            exit(1);
         break;
         default:
            cout<<"Wrong Choice"<<endl;
      }
   }
   return 0;
}
