import copy, unittest
from server import DEFAULT, validate, kotlin
class DesignTests(unittest.TestCase):
    def test_missing_metric_and_control_types_are_rejected(self):
        bad=copy.deepcopy(DEFAULT);bad['metricOrder']=['volume']*4
        with self.assertRaises(ValueError):validate(bad)
        bad=copy.deepcopy(DEFAULT);bad['numberSp']=True
        with self.assertRaises(ValueError):validate(bad)
        bad=copy.deepcopy(DEFAULT);bad['primary']='0); execute()'
        with self.assertRaises(ValueError):validate(bad)
    def test_kotlin_display_strings_escape_code_metacharacters(self):
        data=copy.deepcopy(DEFAULT);data['title']='新标题 "$name" \\ 测试'
        result=kotlin(data)
        self.assertIn('\\$name',result)
        self.assertIn('\\"',result)
        self.assertNotIn('execute()',result)
    def test_all_four_metrics_and_bounds_preserved(self):
        data=copy.deepcopy(DEFAULT);data['metricOrder'].reverse();self.assertEqual(validate(data)['metricOrder'][0],'elapsed')
        data['gapDp']=100
        with self.assertRaises(ValueError):validate(data)
if __name__=='__main__':unittest.main()
